/*
 * prompt.c -- PS1/PS2/PS4 expansion, as bash does it: backslash escapes
 * first (\u \h \w ...), then parameter / command / arithmetic expansion of
 * the result (bash's `promptvars`, on by default).
 *
 * \[ and \] mark text that takes no room on screen (color codes). They are
 * left in the result as the control bytes PROMPT_IGN_START / PROMPT_IGN_END
 * (readline's own convention) so the line editor can work out how wide the
 * prompt really is; vs_prompt_strip() removes them for output that is not
 * going to an editor.
 *
 * Nothing here needs a terminal, so it is tested like any other expansion.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vaporshell.h"
#include "expand.h"
#include "platform.h"
#include "mode.h"
#include "prompt.h"

/* A value coming from the environment (a directory name, a user name) must
 * not be expanded a second time: quote the characters the expansion step
 * would act on.
 */

static void add_literal(struct sbuf_s *out, const char *s)
{
  for (; *s != '\0'; s++)
    {
      if (*s == '$' || *s == '`' || *s == '\\')
        {
          sb_addc(out, '\\');
        }

      sb_addc(out, *s);
    }
}

static const char *base_name(const char *path)
{
  const char *b = strrchr(path, '/');

  return (b != NULL && b[1] != '\0') ? b + 1 : path;
}

/* $PWD with $HOME shown as ~ and, if PROMPT_DIRTRIM is set, only the last
 * few components kept.
 */

static void add_cwd(struct sbuf_s *out, bool basename_only)
{
  const char *pwd = var_get("PWD");
  const char *home = var_get("HOME");
  const char *trim = var_get("PROMPT_DIRTRIM");
  struct sbuf_s tmp;
  size_t hl;
  long keep = trim != NULL ? strtol(trim, NULL, 10) : 0;

  if (pwd == NULL)
    {
      pwd = "?";
    }

  if (basename_only)
    {
      if (home != NULL && strcmp(home, "/") != 0 && strcmp(pwd, home) == 0)
        {
          add_literal(out, "~");
        }
      else
        {
          add_literal(out, strcmp(pwd, "/") == 0 ? "/" : base_name(pwd));
        }

      return;
    }

  sb_init(&tmp);
  hl = home != NULL ? strlen(home) : 0;
  if (hl > 0 && strcmp(home, "/") != 0 && strncmp(pwd, home, hl) == 0 &&
      (pwd[hl] == '\0' || pwd[hl] == '/'))
    {
      sb_addc(&tmp, '~');
      sb_adds(&tmp, pwd + hl);
    }
  else
    {
      sb_adds(&tmp, pwd);
    }

  if (keep > 0)
    {
      long seen = 0;
      size_t i;
      size_t start = tmp.s[0] == '~' ? 1 : 0;

      /* Walk back to the slash that starts the last 'keep' components; if
       * another component lies before it, replace what precedes with "...".
       */

      tmp.s[tmp.len] = '\0';
      for (i = tmp.len; i > start; i--)
        {
          if (tmp.s[i - 1] == '/' && ++seen == keep)
            {
              if (i - 1 > start)
                {
                  add_literal(out, tmp.s[0] == '~' ? "~/..." : "...");
                  add_literal(out, tmp.s + i - 1);
                  sb_free(&tmp);
                  return;
                }

              break;
            }
        }
    }

  tmp.s[tmp.len] = '\0';
  add_literal(out, tmp.s);
  sb_free(&tmp);
}

static void add_time(struct sbuf_s *out, const char *fmt)
{
  char buf[128];
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);

  if (tm != NULL && strftime(buf, sizeof(buf), fmt, tm) > 0)
    {
      sb_adds(out, buf);
    }
}

static void add_number(struct sbuf_s *out, long n)
{
  char buf[24];

  snprintf(buf, sizeof(buf), "%ld", n);
  sb_adds(out, buf);
}

char *vs_prompt_decode(const char *ps)
{
  struct sbuf_s out;
  const char *p;
  char host[128];

  sb_init(&out);
  for (p = ps; *p != '\0'; p++)
    {
      if (*p != '\\' || p[1] == '\0')
        {
          sb_addc(&out, *p);
          continue;
        }

      p++;
      switch (*p)
        {
          case 'a':  sb_addc(&out, '\a'); break;
          case 'e':  sb_addc(&out, '\033'); break;
          case 'n':  sb_addc(&out, '\n'); break;
          case 'r':  sb_addc(&out, '\r'); break;
          case '\\': sb_adds(&out, "\\\\"); break;
          case '[':  sb_addc(&out, PROMPT_IGN_START); break;
          case ']':  sb_addc(&out, PROMPT_IGN_END); break;
          case 'd':  add_time(&out, "%a %b %d"); break;
          case 't':  add_time(&out, "%H:%M:%S"); break;
          case 'T':  add_time(&out, "%I:%M:%S"); break;
          case '@':  add_time(&out, "%I:%M %p"); break;
          case 'A':  add_time(&out, "%H:%M"); break;
          case 'D':
            if (p[1] == '{' && strchr(p + 2, '}') != NULL)
              {
                const char *close = strchr(p + 2, '}');
                char fmt[64];
                size_t n = (size_t)(close - (p + 2));

                if (n >= sizeof(fmt))
                  {
                    n = sizeof(fmt) - 1;
                  }

                memcpy(fmt, p + 2, n);
                fmt[n] = '\0';
                add_time(&out, n > 0 ? fmt : "%X");
                p = close;
              }
            else
              {
                sb_adds(&out, "\\D");
              }

            break;

          case 'h':
          case 'H':
            if (vs_plat_hostname(host, sizeof(host)) != 0)
              {
                strcpy(host, "localhost");
              }

            if (*p == 'h')
              {
                char *dot = strchr(host, '.');

                if (dot != NULL)
                  {
                    *dot = '\0';
                  }
              }

            add_literal(&out, host);
            break;

          case 'j':  add_number(&out, (long)g_sh.njobs); break;
          case 's':  add_literal(&out, g_sh.profile == VS_PROFILE_POSIX ? "sh" : "vaporshell"); break;
          case 'u':
            {
              const char *u = var_get("USER");

              if (u == NULL)
                {
                  u = var_get("LOGNAME");
                }

              add_literal(&out, u != NULL ? u : "user");
            }

            break;

          case 'v':
          case 'V':
            {
              const char *v = var_get("BASH_VERSION");
              size_t n = 0;
              int dots = 0;

              if (v == NULL)
                {
                  v = "0";
                }

              while (v[n] != '\0' && (v[n] != '.' || ++dots < (*p == 'v' ? 2 : 3)))
                {
                  n++;
                }

              sb_addn(&out, v, n);
            }

            break;

          case 'w':  add_cwd(&out, false); break;
          case 'W':  add_cwd(&out, true); break;
          case '!':  add_number(&out, g_sh.hist_num + 1); break;
          case '#':  add_number(&out, g_sh.cmd_num + 1); break;
          case '$':  sb_addc(&out, vs_plat_euid() == 0 ? '#' : '$'); break;

          case '0': case '1': case '2': case '3':
          case '4': case '5': case '6': case '7':
            {
              int v = 0;
              int i;

              for (i = 0; i < 3 && p[0] >= '0' && p[0] <= '7'; i++, p++)
                {
                  v = v * 8 + (p[0] - '0');
                }

              p--;
              sb_addc(&out, (char)v);
            }

            break;

          default:
            sb_addc(&out, '\\');
            sb_addc(&out, *p);
            break;
        }
    }

  return out.s != NULL ? sb_take(&out) : vs_xstrdup("");
}

char *vs_prompt_expand(const char *ps)
{
  char *decoded = vs_prompt_decode(ps);
  char *result;

  if (strpbrk(decoded, "$`") == NULL)
    {
      return decoded;                  /* nothing the second step could change */
    }

  result = expand_heredoc(decoded);
  free(decoded);
  return result != NULL ? result : vs_xstrdup("");
}

void vs_prompt_strip(char *s)
{
  char *w = s;

  for (; *s != '\0'; s++)
    {
      if (*s != PROMPT_IGN_START && *s != PROMPT_IGN_END)
        {
          *w++ = *s;
        }
    }

  *w = '\0';
}
