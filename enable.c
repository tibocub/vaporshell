/*
 * enable.c -- enable -n/-p/-a/name...: turn builtins on and off.
 *
 * A disabled builtin is invisible to command resolution (builtin_find, in
 * builtins.c, checks builtin_is_enabled()): the shell then looks for an
 * external program of the same name instead, exactly as if the builtin did
 * not exist. `enable` itself always finds a builtin by name regardless of
 * its state, since re-enabling one requires being able to find it while
 * disabled.
 *
 * `-f` (load a builtin from a shared object) is not supported: this shell
 * has no such loader, so it is an error rather than a silent no-op.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "exec.h"
#include "mode.h"

static struct disabled_s **find_disabled(const char *name)
{
  struct disabled_s **p = &g_sh.disabled_builtins;

  while (*p != NULL && strcmp((*p)->name, name) != 0)
    {
      p = &(*p)->next;
    }

  return p;
}

bool builtin_is_enabled(const char *name)
{
  return *find_disabled(name) == NULL;
}

static void set_enabled(const char *name, bool enabled)
{
  struct disabled_s **p = find_disabled(name);

  if (enabled)
    {
      if (*p != NULL)
        {
          struct disabled_s *dead = *p;

          *p = dead->next;
          free(dead->name);
          free(dead);
        }

      return;
    }

  if (*p != NULL)
    {
      return;                      /* already disabled */
    }

  {
    struct disabled_s *d = vs_xmalloc(sizeof(*d));

    d->name = vs_xstrdup(name);
    d->next = g_sh.disabled_builtins;
    g_sh.disabled_builtins = d;
  }
}

/* The real, in-table builtin of this name, disabled or not -- unlike
 * builtin_find(), which hides a disabled one from command resolution.
 */

static const struct builtin_s *find_any(const char *name)
{
  const struct builtin_s *b;

  for (b = g_vs_builtins; b->name != NULL; b++)
    {
      if ((b->modes & vs_mode_bit()) != 0 && strcmp(b->name, name) == 0)
        {
          return b;
        }
    }

  return NULL;
}

static int cmp_names(const void *a, const void *b)
{
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* enable -p / bare enable (only the enabled ones, as `enable name` would
 * reproduce them) / enable -a (every one, `enable -n name` for the disabled).
 */

static void list_builtins(bool all)
{
  const struct builtin_s *b;
  const char **names;
  size_t n = 0;
  size_t i;

  for (b = g_vs_builtins; b->name != NULL; b++)
    {
      n++;
    }

  names = vs_xmalloc(n * sizeof(*names));
  n = 0;
  for (b = g_vs_builtins; b->name != NULL; b++)
    {
      if ((b->modes & vs_mode_bit()) != 0)
        {
          names[n++] = b->name;
        }
    }

  qsort(names, n, sizeof(*names), cmp_names);
  for (i = 0; i < n; i++)
    {
      bool en = builtin_is_enabled(names[i]);

      if (all || en)
        {
          printf("enable %s%s\n", en ? "" : "-n ", names[i]);
        }
    }

  free(names);
}

int bi_enable(int argc, char **argv)
{
  bool disable = false;
  bool posix_p = false;
  bool all = false;
  int i = 1;
  int status = 0;

  for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++)
    {
      if (strcmp(argv[i], "--") == 0)
        {
          i++;
          break;
        }

      if (strcmp(argv[i], "-n") == 0)
        {
          disable = true;
        }
      else if (strcmp(argv[i], "-p") == 0)
        {
          posix_p = true;
        }
      else if (strcmp(argv[i], "-a") == 0)
        {
          all = true;
        }
      else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "-d") == 0)
        {
          vs_err("enable: -f: loadable builtins are not supported here");
          return 1;
        }
      else if (strcmp(argv[i], "-s") == 0)
        {
          /* POSIX special builtins only: harmless to accept and ignore,
           * since this listing would still be filtered the same way
           */
        }
      else
        {
          vs_err("enable: %s: invalid option", argv[i]);
          return 2;
        }
    }

  if (i >= argc)
    {
      list_builtins(all || posix_p ? all : false);
      return 0;
    }

  for (; i < argc; i++)
    {
      if (find_any(argv[i]) == NULL)
        {
          vs_err("enable: %s: not a shell builtin", argv[i]);
          status = 1;
          continue;
        }

      set_enabled(argv[i], !disable);
    }

  return status;
}
