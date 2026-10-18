/*
 * history.c -- command history.
 *
 * The list is plain malloc()'d strings, oldest first, trimmed to $HISTSIZE.
 * $HISTFILE is read once at startup and the new entries are appended to it
 * on exit, so shells running side by side merge their histories instead of
 * overwriting each other's.
 */

#include <nuttx/config.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vaporshell.h"
#include "exec.h"
#include "history.h"

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
      g_hist = vs_xrealloc(g_hist, (size_t)g_cap * sizeof(char *));
    }

  g_hist[g_n++] = owned;
  if (g_n > max)
    {
      drop_front(g_n - (int)max);
    }

  g_sh.hist_num = g_base + g_n;
}

void vs_hist_init(void)
{
  const char *home = var_get("HOME");
  const char *file = histfile();
  FILE *f;
  char *line = NULL;
  size_t cap = 0;
  ssize_t len;

  if (file == NULL && home != NULL && *home != '\0')
    {
      char *def = vs_xmalloc(strlen(home) + 16);

      sprintf(def, "%s/.vsh_history", home);
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
          push(vs_xstrdup(line));
        }
    }

  free(line);
  fclose(f);
  g_saved = g_n;
}

/* HISTCONTROL: ignorespace, ignoredups, ignoreboth, erasedups. */

void vs_hist_add(const char *line)
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

  push(vs_xstrdup(line));
}

int vs_hist_count(void)
{
  return g_n;
}

const char *vs_hist_get(int i)
{
  return (i >= 0 && i < g_n) ? g_hist[i] : NULL;
}

static int write_entries(const char *file, int from, bool truncate)
{
  FILE *f = fopen(file, truncate ? "w" : "a");
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

void vs_hist_save(void)
{
  const char *file = histfile();

  if (file != NULL && *file != '\0' && write_entries(file, g_saved, false) == 0)
    {
      g_saved = g_n;
    }
}

void vs_hist_fini(void)
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

            g_base += g_n;
            g_n = g_saved = 0;
            return 0;

          case 'd':
            {
              long off = argc > 2 ? strtol(argv[2], NULL, 10) : 0;
              long idx = off < 0 ? g_n + off : off - g_base - 1;

              if (idx < 0 || idx >= g_n)
                {
                  vs_err("history: %s: history position out of range", argc > 2 ? argv[2] : "");
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
            vs_hist_save();
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
                      push(vs_xstrdup(line));
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
            vs_err("history: %s: invalid option", argv[1]);
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
            vs_err("history: %s: numeric argument required", argv[1]);
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
