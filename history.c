/*
 * history.c -- command history.
 *
 * The list is plain malloc()'d strings, oldest first, trimmed to $HISTSIZE.
 * $HISTFILE is read once at startup and the new entries are appended to it
 * on exit, so shells running side by side merge their histories instead of
 * overwriting each other's.
 */

#include <nuttx/config.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vaporshell.h"
#include "exec.h"
#include "history.h"
#include "expand.h"
#include "mode.h"

#ifdef VAPORSHELL_POSIX
#  define HIST_DEFAULT_SIZE 500
#else
#  define HIST_DEFAULT_SIZE 100         /* a few KB of RAM matter on a microcontroller */
#endif

static char **g_hist;
static int g_n;
static int g_cap;
static long g_base;             /* entries dropped from the front: numbering continues */
static int g_saved;             /* g_hist[0..g_saved) are in HISTFILE already */
static bool g_rewrite;          /* the file is longer than HISTSIZE: rewrite it on save */

static long limit_var(const char *name, long dflt)
{
  const char *v = var_get(name);
  char *end;
  long n;

  if (v == NULL || *v == '\0')
    {
      return dflt;
    }

  n = strtol(v, &end, 10);
  return (*end == '\0' && n >= 0) ? n : dflt;
}

static const char *histfile(void)
{
  return var_get("HISTFILE");
}

static void drop_front(int k)
{
  int i;

  for (i = 0; i < k && i < g_n; i++)
    {
      free(g_hist[i]);
    }

  memmove(g_hist, g_hist + k, (size_t)(g_n - k) * sizeof(char *));
  g_n -= k;
  g_saved = g_saved > k ? g_saved - k : 0;
  g_base += k;
}

static void push(char *owned)
{
  long max = limit_var("HISTSIZE", HIST_DEFAULT_SIZE);

  if (g_n == g_cap)
    {
      g_cap = g_cap > 0 ? g_cap * 2 : 32;
      g_hist = vash_xrealloc(g_hist, (size_t)g_cap * sizeof(char *));
    }

  g_hist[g_n++] = owned;
  if (g_n > max)
    {
      drop_front(g_n - (int)max);
    }

  g_sh.hist_num = g_base + g_n;
}

void vash_hist_init(void)
{
  const char *home = var_get("HOME");
  const char *file = histfile();
  FILE *f;
  char *line = NULL;
  size_t cap = 0;
  ssize_t len;
  long nread = 0;

  if (file == NULL && home != NULL && *home != '\0')
    {
      char *def = vash_xmalloc(strlen(home) + 16);

      sprintf(def, "%s/.vash_history", home);
      var_set("HISTFILE", def);
      free(def);
      file = histfile();
    }

  if (file == NULL || (f = fopen(file, "r")) == NULL)
    {
      return;
    }

  while ((len = getline(&line, &cap, f)) >= 0)
    {
      if (len > 0 && line[len - 1] == '\n')
        {
          line[--len] = '\0';
        }

      if (len > 0)
        {
          push(vash_xstrdup(line));
          nread++;
        }
    }

  free(line);
  fclose(f);
  g_saved = g_n;
  g_rewrite = nread > limit_var("HISTSIZE", HIST_DEFAULT_SIZE);
}

/* HISTCONTROL: ignorespace, ignoredups, ignoreboth, erasedups. */

void vash_hist_add(const char *line)
{
  const char *ctl = var_get("HISTCONTROL");
  int i;

  if (line[0] == '\0')
    {
      return;
    }

  if (ctl != NULL)
    {
      if ((strstr(ctl, "ignorespace") != NULL || strstr(ctl, "ignoreboth") != NULL) &&
          line[0] == ' ')
        {
          return;
        }

      if ((strstr(ctl, "ignoredups") != NULL || strstr(ctl, "ignoreboth") != NULL) &&
          g_n > 0 && strcmp(g_hist[g_n - 1], line) == 0)
        {
          return;
        }

      if (strstr(ctl, "erasedups") != NULL)
        {
          for (i = g_n - 1; i >= 0; i--)
            {
              if (strcmp(g_hist[i], line) == 0)
                {
                  free(g_hist[i]);
                  memmove(g_hist + i, g_hist + i + 1,
                          (size_t)(g_n - i - 1) * sizeof(char *));
                  g_n--;
                  if (i < g_saved)
                    {
                      g_saved--;
                    }
                }
            }
        }
    }

  push(vash_xstrdup(line));
}

int vash_hist_count(void)
{
  return g_n;
}

const char *vash_hist_get(int i)
{
  return (i >= 0 && i < g_n) ? g_hist[i] : NULL;
}

static int write_entries(const char *file, int from, bool truncate)
{
  int fd = open(file, O_WRONLY | O_CREAT | (truncate ? O_TRUNC : O_APPEND), 0600);
  FILE *f = fd >= 0 ? fdopen(fd, "w") : NULL;       /* typed commands often hold secrets: owner only */
  int i;

  if (f == NULL)
    {
      return -1;
    }

  for (i = from; i < g_n; i++)
    {
      fputs(g_hist[i], f);
      fputc('\n', f);
    }

  return fclose(f) == 0 ? 0 : -1;
}

void vash_hist_save(void)
{
  const char *file = histfile();

  if (file != NULL && *file != '\0' &&
      write_entries(file, g_rewrite ? 0 : g_saved, g_rewrite) == 0)
    {
      g_saved = g_n;
      g_rewrite = false;
    }
}

void vash_hist_fini(void)
{
  int i;

  for (i = 0; i < g_n; i++)
    {
      free(g_hist[i]);
    }

  free(g_hist);
  g_hist = NULL;
  g_n = g_cap = g_saved = 0;
  g_base = 0;
}

/* history [n] | -c | -d offset | -a | -w | -r | -s arg... */

int bi_history(int argc, char **argv)
{
  const char *file = histfile();
  int i;

  if (argc >= 2 && argv[1][0] == '-' && argv[1][1] != '\0')
    {
      switch (argv[1][1])
        {
          case 'c':
            for (i = 0; i < g_n; i++)
              {
                free(g_hist[i]);
              }

            g_base = 0;                  /* bash starts numbering again */
            g_n = g_saved = 0;
            g_sh.hist_num = 0;
            return 0;

          case 'd':
            {
              long off = argc > 2 ? strtol(argv[2], NULL, 10) : 0;
              long idx = off < 0 ? g_n + off : off - g_base - 1;

              if (idx < 0 || idx >= g_n)
                {
                  vash_err("history: %s: history position out of range", argc > 2 ? argv[2] : "");
                  return 1;
                }

              free(g_hist[idx]);
              memmove(g_hist + idx, g_hist + idx + 1,
                      (size_t)(g_n - idx - 1) * sizeof(char *));
              g_n--;
              if (idx < g_saved)
                {
                  g_saved--;
                }

              return 0;
            }

          case 'a':
            vash_hist_save();
            return 0;

          case 'w':
            if (file != NULL && write_entries(file, 0, true) == 0)
              {
                g_saved = g_n;
                return 0;
              }

            return 1;

          case 'r':
            {
              FILE *f = file != NULL ? fopen(file, "r") : NULL;
              char *line = NULL;
              size_t cap = 0;
              ssize_t len;

              if (f == NULL)
                {
                  return 1;
                }

              while ((len = getline(&line, &cap, f)) >= 0)
                {
                  if (len > 0 && line[len - 1] == '\n')
                    {
                      line[--len] = '\0';
                    }

                  if (len > 0)
                    {
                      push(vash_xstrdup(line));
                    }
                }

              free(line);
              fclose(f);
              return 0;
            }

          case 's':
            {
              struct sbuf_s b;

              sb_init(&b);
              for (i = 2; i < argc; i++)
                {
                  if (i > 2)
                    {
                      sb_addc(&b, ' ');
                    }

                  sb_adds(&b, argv[i]);
                }

              if (b.s != NULL)
                {
                  push(sb_take(&b));
                }

              return 0;
            }

          default:
            vash_err("history: %s: invalid option", argv[1]);
            return 2;
        }
    }

  {
    long show = g_n;

    if (argc > 1)
      {
        char *end;

        show = strtol(argv[1], &end, 10);
        if (*end != '\0' || show < 0)
          {
            vash_err("history: %s: numeric argument required", argv[1]);
            return 1;
          }
      }

    for (i = g_n > show ? g_n - (int)show : 0; i < g_n; i++)
      {
        printf("%5ld  %s\n", g_base + i + 1, g_hist[i]);
      }
  }

  return 0;
}

/* ---- Word designators and modifiers: !!:2-3 !$ !-2:^ !str:s/a/b/ :gs/a/b/ :h :t :r :e :q ----
 * shortcut: words split on blanks and quotes ('..' ".." and backslash keep a word together);
 * :p and :x are not done, and a missing event or word is an error, as in bash.
 */

static const char *find_event(const char *p, size_t *len);

struct hw_s { const char *s; size_t n; };

static int hist_words(const char *ev, struct hw_s *w, int max)
{
  const char *p = ev;
  int k = 0;

  for (; k < max; )
    {
      char q = 0;

      p += strspn(p, " \t");
      if (*p == '\0')
        {
          break;
        }

      w[k].s = p;
      for (; *p != '\0' && (q != 0 || (*p != ' ' && *p != '\t')); p++)
        {
          if (*p == '\\' && p[1] != '\0')
            {
              p++;
            }
          else if (q == 0 && (*p == '\'' || *p == '"'))
            {
              q = *p;
            }
          else if (*p == q)
            {
              q = 0;
            }
        }

      w[k].n = (size_t)(p - w[k].s);
      k++;
    }

  return k;
}

static char *g_sub_old;
static char *g_sub_new;

/* Applies one :modifier (p points after the colon) to the text in 'o' from 'from'. Returns the
 * length consumed, or 0 if it is not a modifier (or failed: *bad is set).
 */

static size_t hist_modify(struct sbuf_s *o, size_t from, const char *p, bool *bad)
{
  size_t used = 0;
  bool global = false;
  char *text = vash_xstrndup(o->s + from, o->len - from);
  struct sbuf_s r;
  char m;

  if (*p == 'g')
    {
      global = true;
      used++;
    }

  m = p[used++];
  sb_init(&r);
  if (m == 'h' || m == 't' || m == 'r' || m == 'e')
    {
      const char *slash = strrchr(text, '/');
      const char *dot = strrchr(slash != NULL ? slash : text, '.');

      if (m == 'h')
        {
          sb_addn(&r, text, slash != NULL ? (size_t)(slash - text) : strlen(text));
        }
      else if (m == 't')
        {
          sb_adds(&r, slash != NULL ? slash + 1 : text);
        }
      else if (m == 'r')
        {
          sb_addn(&r, text, dot != NULL && dot != (slash != NULL ? slash + 1 : text) ? (size_t)(dot - text) : strlen(text));
        }
      else
        {
          sb_adds(&r, dot != NULL ? dot : "");
        }
    }
  else if (m == 'q')
    {
      char *qd = vash_quote_word(text);

      sb_adds(&r, qd);
      free(qd);
    }
  else if (m == 's' || m == '&')
    {
      if (m == 's')
        {
          char d = p[used];
          const char *a = p + used + 1;
          const char *e1 = d != '\0' ? strchr(a, d) : NULL;
          const char *e2;
          struct sbuf_s so;
          struct sbuf_s sn;

          if (e1 == NULL)
            {
              *bad = true;
              goto out;
            }

          e2 = strchr(e1 + 1, d);
          sb_init(&so);
          sb_init(&sn);
          sb_addn(&so, a, (size_t)(e1 - a));
          sb_addn(&sn, e1 + 1, e2 != NULL ? (size_t)(e2 - e1 - 1) : strlen(e1 + 1));
          free(g_sub_old);
          free(g_sub_new);
          g_sub_old = sb_take(&so);
          g_sub_new = sb_take(&sn);
          used = (size_t)((e2 != NULL ? e2 + 1 : e1 + 1 + strlen(e1 + 1)) - p);
        }

      if (g_sub_old == NULL || g_sub_old[0] == '\0')
        {
          *bad = true;
          goto out;
        }

      {
        const char *t = text;
        bool done = false;
        const char *hit;

        while ((hit = strstr(t, g_sub_old)) != NULL && (global || !done))
          {
            const char *nw;

            sb_addn(&r, t, (size_t)(hit - t));
            for (nw = g_sub_new; *nw != '\0'; nw++)
              {
                if (*nw == '&')
                  {
                    sb_adds(&r, g_sub_old);
                  }
                else
                  {
                    if (*nw == '\\' && nw[1] != '\0')
                      {
                        nw++;
                      }

                    sb_addc(&r, *nw);
                  }
              }

            t = hit + strlen(g_sub_old);
            done = true;
          }

        if (!done)
          {
            *bad = true;
            goto out;
          }

        sb_adds(&r, t);
      }
    }
  else
    {
      used = 0;
      goto out;
    }

  o->len = from;
  sb_adds(o, r.s != NULL ? r.s : "");
  if (o->s != NULL)
    {
      o->s[o->len] = '\0';
    }

out:
  sb_free(&r);
  free(text);
  return *bad ? 0 : used;
}

/* Expands the event at p (a '!'): appends to o, sets *len to what it covered. false: error. */

static const char *hist_event(struct sbuf_s *o, const char *p, size_t *len)
{
  const char *ev;
  const char *q;
  struct hw_s w[128];
  int nw;
  int first = 0;
  int last = -1;
  bool have_spec = false;
  bool empty_ok = false;
  size_t from = o->len;
  const char *msg = "event not found";

  static char *sofar;                   /* !#'s text, kept until the next event */

  free(sofar);
  sofar = NULL;
  if (p[1] == '#')
    {
      sofar = vash_xstrndup(o->s != NULL ? o->s : "", o->len);      /* !#: the line typed so far */
      ev = sofar;
      *len = 2;
    }
  else if (strchr(":$^*%", p[1]) != NULL)
    {
      ev = g_n > 0 ? g_hist[g_n - 1] : NULL;
      *len = 1;
    }
  else
    {
      ev = find_event(p, len);
    }

  if (ev == NULL)
    {
      return msg;
    }

  q = p + *len;
  nw = hist_words(ev, w, 128);
  if (*q == ':' && (isdigit((unsigned char)q[1]) || (q[1] != '\0' && strchr("^$*-%", q[1]) != NULL)))
    {
      q++;
      have_spec = true;
    }
  else if (*q != '\0' && strchr("^$*%", *q) != NULL)
    {
      have_spec = true;
    }

  if (have_spec)
    {
      msg = "bad word specifier";
      if (isdigit((unsigned char)*q))
        {
          char *e;

          first = (int)strtol(q, &e, 10);
          last = first;
          q = e;
          if (*q == '-')
            {
              q++;
              if (isdigit((unsigned char)*q))
                {
                  last = (int)strtol(q, &e, 10);
                  q = e;
                }
              else
                {
                  last = nw - 2;
                }
            }
          else if (*q == '*')
            {
              last = nw - 1;
              q++;
            }
        }
      else if (*q == '-')
        {
          char *e;

          q++;
          first = 0;
          last = (int)strtol(q, &e, 10);
          q = e;
        }
      else if (*q == '^')
        {
          first = last = 1;
          q++;
        }
      else if (*q == '$')
        {
          first = last = nw - 1;
          q++;
        }
      else if (*q == '*')
        {
          first = 1;
          last = nw - 1;
          q++;
          if (nw < 2)
            {
              empty_ok = true;         /* "!*" with no arguments is empty, not an error */
              last = 0;
            }
        }
      else
        {
          return msg;                  /* %: needs the last !?str? word, not done */
        }

      if (first < 0 || last >= nw || last < first - 1 || (last == first - 1 && !empty_ok))
        {
          *len = (size_t)(q - p);
          return msg;
        }

      {
        int k;

        for (k = first; k <= last; k++)
          {
            sb_adds(o, k > first ? " " : "");
            sb_addn(o, w[k].s, w[k].n);
          }
      }
    }
  else
    {
      sb_adds(o, ev);
    }

  for (; *q == ':'; )
    {
      bool bad = false;
      size_t u = hist_modify(o, from, q + 1, &bad);

      if (u == 0)
        {
          if (bad)
            {
              *len = (size_t)(q - p);
              return "modifier failed";
            }

          break;
        }

      q += 1 + u;
    }

  *len = (size_t)(q - p);
  return NULL;
}

/* The entry an event names: !! !n !-n !prefix !?text?; NULL if none. *len gets the event's length. */

static const char *find_event(const char *p, size_t *len)
{
  const char *q = p + 1;
  char *pat;
  size_t n;
  int i;

  if (*q == '!')
    {
      *len = 2;
      return g_n > 0 ? g_hist[g_n - 1] : NULL;
    }

  if (isdigit((unsigned char)*q) || (*q == '-' && isdigit((unsigned char)q[1])))
    {
      char *end;
      long v = strtol(q, &end, 10);

      *len = (size_t)(end - p);
      v = v < 0 ? g_n + v : v - g_base - 1;
      return v >= 0 && v < g_n ? g_hist[v] : NULL;
    }

  if (*q == '?')
    {
      const char *close = strchr(q + 1, '?');

      n = close != NULL ? (size_t)(close - q - 1) : strlen(q + 1);
      *len = n + (close != NULL ? 3 : 2);
      q++;
    }
  else
    {
      n = strcspn(q, " \t\n;|&()<>'\"$^*:");
      *len = n + 1;
    }

  pat = vash_xstrndup(q, n);
  for (i = g_n - 1; n > 0 && i >= 0; i--)
    {
      if (p[1] == '?' ? strstr(g_hist[i], pat) != NULL : strncmp(g_hist[i], pat, n) == 0)
        {
          break;
        }
    }

  free(pat);
  return i >= 0 && n > 0 ? g_hist[i] : NULL;
}

/* bash's history expansion: !! !$ !^ !* !n !-n !prefix !?text? (each with :$ :^ :* :n) and
 * ^old^new^. NULL when the line holds none; an unknown event prints an error and gives "".
 * Single quotes and a backslash turn it off.
 */

char *vash_hist_expand(const char *line)
{
  struct sbuf_s o;
  bool sq = false;
  bool dq = false;
  bool changed = false;
  const char *p = line;

  sb_init(&o);
  if (line[0] == '^' && g_n > 0)
    {
      const char *mid = strchr(line + 1, '^');
      const char *last = g_hist[g_n - 1];
      char *old = mid != NULL ? vash_xstrndup(line + 1, (size_t)(mid - line - 1)) : NULL;
      const char *at = old != NULL && *old != '\0' ? strstr(last, old) : NULL;

      if (at == NULL)
        {
          vash_err("%s: substitution failed", line);
          free(old);
          return vash_xstrdup("");
        }

      sb_addn(&o, last, (size_t)(at - last));
      sb_addn(&o, mid + 1, strcspn(mid + 1, "^"));
      sb_adds(&o, at + strlen(old));
      free(old);
      return sb_take(&o);
    }

  while (*p != '\0')
    {
      char c = *p;
      size_t len;

      if (c == '\\' && p[1] != '\0')
        {
          sb_addn(&o, p, 2);
          p += 2;
          continue;
        }

      if (c == '\'' && !dq)
        {
          sq = !sq;
        }
      else if (c == '"' && !sq)
        {
          dq = !dq;
        }

      if (c != '!' || sq || p[1] == '\0' || strchr(" \t\n=(", p[1]) != NULL)
        {
          sb_addc(&o, c);
          p++;
          continue;
        }

      {
        const char *err = hist_event(&o, p, &len);

        if (err != NULL)
          {
            vash_err("%.*s: %s", (int)len, p, err);
            sb_free(&o);
            return vash_xstrdup("");
          }
      }

      changed = true;
      p += len;
    }

  if (!changed)
    {
      sb_free(&o);
      return NULL;
    }

  return sb_take(&o);
}

/* An entry named by number (negative: from the end) or by its first characters. */

static int fc_ref(const char *a, int dflt, int end)
{
  char *e;
  long n;
  int i;

  if (a == NULL)
    {
      return dflt;
    }

  n = strtol(a, &e, 10);
  if (*e == '\0')
    {
      n = n < 0 ? end + n : n - g_base - 1;
      return n < 0 ? 0 : n >= end ? end - 1 : (int)n;
    }

  for (i = end - 1; i >= 0; i--)
    {
      if (strncmp(g_hist[i], a, strlen(a)) == 0)
        {
          return i;
        }
    }

  vash_err("fc: no command found");
  return -1;
}

/* fc [-lnr] [first [last]] | fc -s [old=new] [cmd]. No editor: fc without -l or -s is refused.
 * The fc line itself is already the newest entry; it is left out, and -s puts the command there.
 */

int bi_fc(int argc, char **argv)
{
  int end = g_n > 0 && strncmp(g_hist[g_n - 1], "fc", 2) == 0 ? g_n - 1 : g_n;
  bool list = false;
  bool nonum = false;
  bool rev = false;
  bool redo = false;
  int i = 1;
  int a;
  int b;

  for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0' && !isdigit((unsigned char)argv[i][1]); i++)
    {
      const char *o;

      for (o = argv[i] + 1; *o != '\0'; o++)
        {
          switch (*o)
            {
              case 'l': list = true; break;
              case 'n': nonum = true; break;
              case 'r': rev = true; break;
              case 's': redo = true; break;
              default:
                vash_err("fc: -%c: not supported (use -l or -s)", *o);
                return 2;
            }
        }
    }

  if (end == 0)
    {
      vash_err("fc: no command found");
      return 1;
    }

  if (redo)
    {
      const char *sub = i < argc && strchr(argv[i], '=') != NULL ? argv[i++] : NULL;
      const char *eq = sub != NULL ? strchr(sub, '=') : NULL;
      char *old = eq != NULL ? vash_xstrndup(sub, (size_t)(eq - sub)) : NULL;
      const char *at;
      struct sbuf_s o;
      char *cmd;
      int status;

      a = fc_ref(i < argc ? argv[i] : NULL, end - 1, end);
      if (a < 0)
        {
          free(old);
          return 1;
        }

      sb_init(&o);
      at = old != NULL && *old != '\0' ? strstr(g_hist[a], old) : NULL;
      if (at != NULL)
        {
          sb_addn(&o, g_hist[a], (size_t)(at - g_hist[a]));
          sb_adds(&o, eq + 1);
          sb_adds(&o, at + strlen(old));
        }
      else
        {
          sb_adds(&o, g_hist[a]);
        }

      free(old);
      cmd = sb_take(&o);
      fprintf(stderr, "%s\n", cmd);
      if (end < g_n)
        {
          free(g_hist[end]);
          g_hist[end] = vash_xstrdup(cmd);
        }

      status = run_string(cmd, strlen(cmd));
      free(cmd);
      return status;
    }

  if (!list)
    {
      vash_err("fc: editing is not supported (use -l or -s)");
      return 2;
    }

  a = fc_ref(i < argc ? argv[i] : NULL, end > 16 ? end - 16 : 0, end);
  b = fc_ref(i + 1 < argc ? argv[i + 1] : NULL, end - 1, end);
  if (a < 0 || b < 0)
    {
      return 1;
    }

  if (a > b)
    {
      int t = a;

      a = b;
      b = t;
      rev = !rev;
    }

  for (i = rev ? b : a; i >= a && i <= b; i += rev ? -1 : 1)
    {
      if (nonum)
        {
          printf(vash_feat(VF_BASH_INFO_FORMATS) ? "\t %s\n" : "\t%s\n", g_hist[i]);   /* bash: a blank (or *) marks edited entries */
        }
      else
        {
          printf(vash_feat(VF_BASH_INFO_FORMATS) ? "%ld\t %s\n" : "%ld\t%s\n", g_base + i + 1, g_hist[i]);
        }
    }

  return 0;
}
