/*
 * builtins_io.c -- echo and printf.
 *
 * printf is the same in bash and dash apart from bash's -v and %q (measured,
 * docs/modes.md), so it is one implementation with those extras behind
 * VF_PRINTF_EXT. echo really does differ: bash takes -n/-e/-E and leaves
 * escapes off; dash (VF_ECHO_XPG) always interprets them and only knows -n.
 */

#include <nuttx/config.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vaporshell.h"
#include "expand.h"
#include "exec.h"

/* Escape flavours, combined per caller. */


static int hexval(int c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static void put_utf8(struct sbuf_s *out, unsigned long cp)
{
  if (cp < 0x80)
    {
      sb_addc(out, (char)cp);
    }
  else if (cp < 0x800)
    {
      sb_addc(out, (char)(0xc0 | (cp >> 6)));
      sb_addc(out, (char)(0x80 | (cp & 0x3f)));
    }
  else if (cp < 0x10000)
    {
      sb_addc(out, (char)(0xe0 | (cp >> 12)));
      sb_addc(out, (char)(0x80 | ((cp >> 6) & 0x3f)));
      sb_addc(out, (char)(0x80 | (cp & 0x3f)));
    }
  else
    {
      sb_addc(out, (char)(0xf0 | (cp >> 18)));
      sb_addc(out, (char)(0x80 | ((cp >> 12) & 0x3f)));
      sb_addc(out, (char)(0x80 | ((cp >> 6) & 0x3f)));
      sb_addc(out, (char)(0x80 | (cp & 0x3f)));
    }
}

/* s points just after a backslash. Appends the character it denotes to
 * out and returns how many characters of s were used. *stop is set for
 * \c. An escape that is not recognized yields the backslash and the
 * character, as both shells do.
 */

size_t vs_esc_one(const char *s, unsigned flags, struct sbuf_s *out,
                  bool *stop)
{
  size_t n;
  int i;

  switch (s[0])
    {
      case '\\': sb_addc(out, '\\'); return 1;
      case 'a':  sb_addc(out, '\a'); return 1;
      case 'b':  sb_addc(out, '\b'); return 1;
      case 'f':  sb_addc(out, '\f'); return 1;
      case 'n':  sb_addc(out, '\n'); return 1;
      case 'r':  sb_addc(out, '\r'); return 1;
      case 't':  sb_addc(out, '\t'); return 1;
      case 'v':  sb_addc(out, '\v'); return 1;
      case 'c':
        if ((flags & ESC_STOP) != 0)
          {
            *stop = true;
            return 1;
          }

        if ((flags & ESC_CTRL) != 0 && s[1] != '\0')
          {
            /* $'\cA' is Ctrl-A: the character with its 0x40 bit flipped */

            sb_addc(out, (char)(((s[1] >= 'a' && s[1] <= 'z') ? s[1] - 32 : s[1]) ^ 0x40));
            return 2;
          }

        break;
      case 'e':
      case 'E':
        if ((flags & ESC_E) != 0)
          {
            sb_addc(out, '\033');
            return 1;
          }

        break;
      case '"':
      case '\'':
      case '?':
        if ((flags & ESC_HEXU) != 0)
          {
            sb_addc(out, s[0]);
            return 1;
          }

        break;
      case 'x':
        if ((flags & ESC_HEXU) != 0 && hexval(s[1]) >= 0)
          {
            int v = hexval(s[1]);

            n = 2;
            if (hexval(s[2]) >= 0)
              {
                v = v * 16 + hexval(s[2]);
                n = 3;
              }

            sb_addc(out, (char)v);
            return n;
          }

        break;
      case 'u':
      case 'U':
        if ((flags & ESC_HEXU) != 0 && hexval(s[1]) >= 0)
          {
            unsigned long cp = 0;
            int maxd = (s[0] == 'u') ? 4 : 8;

            for (i = 1; i <= maxd && hexval(s[i]) >= 0; i++)
              {
                cp = cp * 16 + (unsigned long)hexval(s[i]);
              }

            put_utf8(out, cp);
            return (size_t)i;
          }

        break;
      default:
        break;
    }

  /* Octal: \0ddd (up to 3 digits after the 0) always; \ddd when allowed. */

  if (s[0] == '0' || ((flags & ESC_OCT_PLAIN) != 0 && s[0] >= '1' && s[0] <= '7'))
    {
      int v = 0;
      size_t used = 0;
      size_t maxd = 3;

      if (s[0] == '0')
        {
          used = 1;             /* the 0 itself, then up to 3 more digits */
          for (i = 0; i < (int)maxd && s[used] >= '0' && s[used] <= '7'; i++)
            {
              v = v * 8 + (s[used++] - '0');
            }
        }
      else
        {
          for (i = 0; i < (int)maxd && s[used] >= '0' && s[used] <= '7'; i++)
            {
              v = v * 8 + (s[used++] - '0');
            }
        }

      sb_addc(out, (char)v);
      return used;
    }

  sb_addc(out, '\\');
  if (s[0] != '\0')
    {
      sb_addc(out, s[0]);
      return 1;
    }

  return 0;
}

/* ---- echo ------------------------------------------------------------------ */

int bi_echo(int argc, char **argv)
{
  bool newline = true;
  bool escapes;
  unsigned flags;
  struct sbuf_s out;
  bool stop = false;
  int i = 1;

  if (vs_feat(VF_ECHO_XPG))
    {
      /* dash: only a lone -n, escapes always on. */

      escapes = true;
      flags = ESC_STOP | ESC_OCT_PLAIN | ESC_E;
      if (i < argc && strcmp(argv[i], "-n") == 0)
        {
          newline = false;
          i++;
        }
    }
  else
    {
      /* bash: leading arguments made only of n, e and E are options. */

      escapes = false;
      flags = ESC_STOP | ESC_HEXU | ESC_E;
      for (; i < argc; i++)
        {
          const char *a = argv[i];
          size_t k;

          if (a[0] != '-' || a[1] == '\0')
            {
              break;
            }

          for (k = 1; a[k] == 'n' || a[k] == 'e' || a[k] == 'E'; k++)
            {
            }

          if (a[k] != '\0')
            {
              break;
            }

          for (k = 1; a[k] != '\0'; k++)
            {
              if (a[k] == 'n') newline = false;
              else if (a[k] == 'e') escapes = true;
              else escapes = false;
            }
        }
    }

  sb_init(&out);
  for (; i < argc && !stop; i++)
    {
      const char *s = argv[i];

      for (; *s != '\0' && !stop; )
        {
          if (escapes && *s == '\\')
            {
              s++;
              s += vs_esc_one(s, flags, &out, &stop);
            }
          else
            {
              sb_addc(&out, *s++);
            }
        }

      if (i + 1 < argc && !stop)
        {
          sb_addc(&out, ' ');
        }
    }

  if (newline && !stop)
    {
      sb_addc(&out, '\n');
    }

  if (out.len > 0)
    {
      fwrite(out.s, 1, out.len, stdout);
    }

  sb_free(&out);
  return 0;
}

/* ---- printf ------------------------------------------------------------------ */

struct pf_s
{
  int argc;
  char **argv;
  int next;                   /* index of the next argument */
  bool used_arg;              /* a conversion consumed an argument */
  int status;
  struct sbuf_s out;
  bool stop;
};

static const char *pf_arg(struct pf_s *pf)
{
  pf->used_arg = true;
  return pf->next < pf->argc ? pf->argv[pf->next++] : NULL;
}

/* Numeric argument: 'c or "c is that character's value, otherwise a
 * C-style number (0x.., 0..). Bad input reports and yields what parsed.
 */

static long long pf_number(struct pf_s *pf, const char *s, bool is_unsigned)
{
  char *end;
  long long v;

  if (s == NULL || s[0] == '\0')
    {
      return 0;
    }

  if ((s[0] == '\'' || s[0] == '"') && s[1] != '\0')
    {
      return (unsigned char)s[1];
    }

  errno = 0;
  if (is_unsigned)
    {
      v = (long long)strtoull(s, &end, 0);
    }
  else
    {
      v = strtoll(s, &end, 0);
    }

  if (end == s || *end != '\0' || errno == ERANGE)
    {
      vs_err("printf: %s: invalid number", s);
      pf->status = 1;
    }

  return end == s ? 0 : v;
}

/* One pass over the format; sets pf->stop when output must end. */

static void pf_run(struct pf_s *pf, const char *fmt)
{
  unsigned eflags = vs_feat(VF_PRINTF_EXT) ? (ESC_OCT_PLAIN | ESC_HEXU | ESC_E)
                                           : ESC_OCT_PLAIN;
  const char *p = fmt;

  while (*p != '\0' && !pf->stop)
    {
      if (*p == '\\')
        {
          p++;
          p += vs_esc_one(p, eflags, &pf->out, &pf->stop);
          continue;
        }

      if (*p != '%')
        {
          sb_addc(&pf->out, *p++);
          continue;
        }

      p++;
      if (*p == '%')
        {
          sb_addc(&pf->out, '%');
          p++;
          continue;
        }

      {
        char spec[64];
        size_t sl = 0;
        const char *a;
        char conv;

        spec[sl++] = '%';

        while (*p != '\0' && strchr("-+ #0", *p) != NULL && sl < sizeof(spec) - 8)
          {
            spec[sl++] = *p++;
          }

        if (*p == '*')
          {
            long long w = pf_number(pf, pf_arg(pf), false);

            sl += (size_t)snprintf(spec + sl, sizeof(spec) - sl, "%lld", w);
            p++;
          }
        else
          {
            while (*p >= '0' && *p <= '9' && sl < sizeof(spec) - 8)
              {
                spec[sl++] = *p++;
              }
          }

        if (*p == '.')
          {
            spec[sl++] = *p++;
            if (*p == '*')
              {
                long long w = pf_number(pf, pf_arg(pf), false);

                sl += (size_t)snprintf(spec + sl, sizeof(spec) - sl, "%lld", w);
                p++;
              }
            else
              {
                while (*p >= '0' && *p <= '9' && sl < sizeof(spec) - 8)
                  {
                    spec[sl++] = *p++;
                  }
              }
          }

        if (*p == '(' && vs_feat(VF_PRINTF_EXT))
          {
            /* %(strftime-format)T: the format is everything up to the ')'
             * that balances this '(' -- parentheses inside it nest, so
             * "%((%Y))T" formats with "(%Y)". The value is an epoch time;
             * -1 (or no argument at all) means now, -2 the shell's start.
             */

            const char *fs = ++p;
            int depth = 1;
            const char *a;
            long long v;
            time_t t;
            struct tm tmv;
            char tfmt[256];
            char buf[256];
            size_t flen;
            size_t need;

            while (*p != '\0' && depth > 0)
              {
                if (*p == '(')
                  {
                    depth++;
                  }
                else if (*p == ')')
                  {
                    depth--;
                  }

                if (depth > 0)
                  {
                    p++;
                  }
              }

            if (depth != 0 || p[1] != 'T')
              {
                vs_err("printf: `%c': invalid time format specification",
                       p[1] != '\0' ? p[1] : '\0');
                pf->status = 1;
                pf->stop = true;
                return;
              }

            flen = (size_t)(p - fs);
            if (flen >= sizeof(tfmt))
              {
                flen = sizeof(tfmt) - 1;
              }

            memcpy(tfmt, fs, flen);
            tfmt[flen] = '\0';
            if (flen == 0)
              {
                strcpy(tfmt, "%X");               /* bash's default: the locale's own time format */
              }

            p += 2;                              /* the ')' and the 'T' */

            a = pf_arg(pf);
            if (a == NULL)
              {
                t = time(NULL);
              }
            else
              {
                v = pf_number(pf, a, false);
                if (v == -1)
                  {
                    t = time(NULL);
                  }
                else if (v == -2)
                  {
                    t = g_sh.seconds_base;       /* the shell's start time */
                  }
                else
                  {
                    t = (time_t)v;
                  }
              }

            {
              /* tzset()/localtime_r() read TZ from the real process
               * environment, which `export` does not update immediately
               * (variables only reach environ when a child is started) --
               * so sync it here from the shell's own value first.
               */

              const char *tz = var_get("TZ");

              if (tz != NULL)
                {
                  setenv("TZ", tz, 1);
                }
              else
                {
                  unsetenv("TZ");
                }
            }

            tzset();
            if (localtime_r(&t, &tmv) == NULL)
              {
                buf[0] = '\0';
                need = 0;
              }
            else
              {
                need = strftime(buf, sizeof(buf), tfmt, &tmv);
              }

            spec[sl++] = 's';
            spec[sl] = '\0';
            {
              char *out;
              int wneed = snprintf(NULL, 0, spec, need > 0 ? buf : "");

              out = vs_xmalloc((size_t)wneed + 1);
              snprintf(out, (size_t)wneed + 1, spec, need > 0 ? buf : "");
              sb_adds(&pf->out, out);
              free(out);
            }

            continue;
          }

        conv = *p;
        if (conv == '\0')
          {
            vs_err("printf: `%%': missing format character");
            pf->status = 1;
            pf->stop = true;
            return;
          }

        p++;
        switch (conv)
          {
            case 'd':
            case 'i':
              {
                char buf[128];
                long long v = pf_number(pf, pf_arg(pf), false);

                spec[sl++] = 'l';
                spec[sl++] = 'l';
                spec[sl++] = 'd';
                spec[sl] = '\0';
                snprintf(buf, sizeof(buf), spec, v);
                sb_adds(&pf->out, buf);
              }

              break;

            case 'o':
            case 'u':
            case 'x':
            case 'X':
              {
                char buf[128];
                unsigned long long v =
                  (unsigned long long)pf_number(pf, pf_arg(pf), true);

                spec[sl++] = 'l';
                spec[sl++] = 'l';
                spec[sl++] = conv;
                spec[sl] = '\0';
                snprintf(buf, sizeof(buf), spec, v);
                sb_adds(&pf->out, buf);
              }

              break;

            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
            case 'a': case 'A':
              {
                char buf[512];
                const char *s = pf_arg(pf);
                double v = (s != NULL && s[0] != '\0') ? strtod(s, NULL) : 0.0;

                spec[sl++] = conv;
                spec[sl] = '\0';
                snprintf(buf, sizeof(buf), spec, v);
                sb_adds(&pf->out, buf);
              }

              break;

            case 'c':
              a = pf_arg(pf);
              spec[sl++] = 's';
              spec[sl] = '\0';
              {
                char one[2];
                int need;
                char *buf;

                one[0] = (a != NULL) ? a[0] : '\0';
                one[1] = '\0';
                need = snprintf(NULL, 0, spec, one);
                buf = vs_xmalloc((size_t)need + 1);
                snprintf(buf, (size_t)need + 1, spec, one);
                sb_adds(&pf->out, buf);
                free(buf);
              }

              break;

            case 's':
            case 'b':
            case 'q':
              {
                struct sbuf_s tmp;
                const char *str;
                int need;
                char *buf;

                if (conv == 'q' && !vs_feat(VF_PRINTF_EXT))
                  {
                    vs_err("printf: `%c': invalid format character", conv);
                    pf->status = vs_feat(VF_EXIT2_ON_ERROR) ? 2 : 1;
                    pf->stop = true;
                    return;
                  }

                a = pf_arg(pf);
                str = a != NULL ? a : "";
                sb_init(&tmp);

                if (conv == 'b')
                  {
                    unsigned bflags = ESC_STOP | (vs_feat(VF_PRINTF_EXT)
                                      ? (ESC_HEXU | ESC_E)
                                      : (ESC_OCT_PLAIN | ESC_E));
                    bool bstop = false;

                    while (*str != '\0' && !bstop)
                      {
                        if (*str == '\\')
                          {
                            str++;
                            str += vs_esc_one(str, bflags, &tmp, &bstop);
                          }
                        else
                          {
                            sb_addc(&tmp, *str++);
                          }
                      }

                    if (bstop)
                      {
                        pf->stop = true;
                      }
                  }
                else if (conv == 'q')
                  {
                    /* shell-quote as bash does: a backslash before anything
                     * unsafe, or $'...' for a string with control characters
                     */

                    const char *s0 = str;
                    bool ctrl = false;

                    for (; *s0 != '\0'; s0++)
                      {
                        ctrl = ctrl || (unsigned char)*s0 < 0x20 || *s0 == 0x7f;
                      }

                    if (ctrl)
                      {
                        char *qw = vs_quote_word(str);

                        sb_adds(&tmp, qw);
                        free(qw);
                        str += strlen(str);
                      }
                    else if (str[0] == '\0')
                      {
                        sb_adds(&tmp, "''");
                      }

                    for (s0 = str; *str != '\0'; str++)
                      {
                        bool safe = (*str >= 'a' && *str <= 'z') ||
                                    (*str >= 'A' && *str <= 'Z') ||
                                    (*str >= '0' && *str <= '9') ||
                                    strchr("_-./:%+=@", *str) != NULL ||
                                    (str != s0 && (*str == '#' || *str == '~')) ||
                                    ((unsigned char)*str >= 0x80 && MB_CUR_MAX > 1);

                        if (!safe)
                          {
                            sb_addc(&tmp, '\\');
                          }

                        sb_addc(&tmp, *str);
                      }
                  }
                else
                  {
                    sb_adds(&tmp, str);
                  }

                spec[sl++] = 's';
                spec[sl] = '\0';
                need = snprintf(NULL, 0, spec, tmp.s != NULL ? tmp.s : "");
                buf = vs_xmalloc((size_t)need + 1);
                snprintf(buf, (size_t)need + 1, spec, tmp.s != NULL ? tmp.s : "");
                sb_adds(&pf->out, buf);
                free(buf);
                sb_free(&tmp);
              }

              break;

            default:
              vs_err("printf: `%c': invalid format character", conv);
              pf->status = vs_feat(VF_EXIT2_ON_ERROR) ? 2 : 1;
              pf->stop = true;
              return;
          }
      }
    }
}

int bi_printf(int argc, char **argv)
{
  struct pf_s pf;
  const char *var = NULL;
  const char *fmt;
  int i = 1;
  int status;

  if (vs_feat(VF_PRINTF_EXT) && i < argc && strcmp(argv[i], "-v") == 0)
    {
      if (i + 1 >= argc || !is_valid_name(argv[i + 1], strlen(argv[i + 1])))
        {
          vs_err("printf: usage: printf [-v var] format [arguments]");
          return 2;
        }

      var = argv[i + 1];
      i += 2;
    }

  if (i < argc && strcmp(argv[i], "--") == 0)
    {
      i++;
    }
  else if (i < argc && argv[i][0] == '-' && argv[i][1] != '\0')
    {
      vs_err("printf: %s: invalid option", argv[i]);
      return 2;
    }

  if (i >= argc)
    {
      vs_err("printf: usage: printf %sformat [arguments]",
             vs_feat(VF_PRINTF_EXT) ? "[-v var] " : "");
      return 2;
    }

  fmt = argv[i++];
  memset(&pf, 0, sizeof(pf));
  pf.argc = argc;
  pf.argv = argv;
  pf.next = i;
  sb_init(&pf.out);

  /* The format is reused while arguments remain and it consumes some. */

  do
    {
      pf.used_arg = false;
      pf_run(&pf, fmt);
    }
  while (pf.next < pf.argc && pf.used_arg && !pf.stop);

  status = pf.status;
  if (var != NULL)
    {
      if (var_set(var, pf.out.s != NULL ? pf.out.s : "") != 0)
        {
          status = 1;
        }
    }
  else if (pf.out.len > 0)
    {
      fwrite(pf.out.s, 1, pf.out.len, stdout);
    }

  sb_free(&pf.out);
  return status;
}
