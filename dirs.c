/*
 * dirs.c -- the directory stack: pushd, popd and dirs (bash).
 *
 * g_sh.dirstack holds the saved directories, the most recently pushed
 * first; the working directory is not in it. `dirs` shows the working
 * directory and then the stack, with $HOME written as ~ unless -l is given.
 */

#include <nuttx/config.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vaporshell.h"
#include "exec.h"
#include "platform.h"

static bool cwd_of(char *buf, size_t n)
{
  const char *pwd = var_get("PWD");

  if (pwd != NULL && pwd[0] == '/' && strlen(pwd) < n)
    {
      strcpy(buf, pwd);
      return true;
    }

  return getcwd(buf, n) != NULL;
}

static int change_dir(const char *dir)
{
  char *argv[3];
  const struct builtin_s *cd = builtin_find("cd");

  argv[0] = "cd";
  argv[1] = (char *)dir;
  argv[2] = NULL;
  return cd->fn(2, argv);
}

static void push_saved(const char *dir)
{
  g_sh.dirstack = vs_xrealloc(g_sh.dirstack, (size_t)(g_sh.ndirs + 1) * sizeof(char *));
  memmove(g_sh.dirstack + 1, g_sh.dirstack, (size_t)g_sh.ndirs * sizeof(char *));
  g_sh.dirstack[0] = vs_xstrdup(dir);
  g_sh.ndirs++;
}

static void drop_saved(int i)
{
  free(g_sh.dirstack[i]);
  memmove(g_sh.dirstack + i, g_sh.dirstack + i + 1,
          (size_t)(g_sh.ndirs - i - 1) * sizeof(char *));
  g_sh.ndirs--;
}

void dirstack_free(void)
{
  while (g_sh.ndirs > 0)
    {
      drop_saved(g_sh.ndirs - 1);
    }

  free(g_sh.dirstack);
  g_sh.dirstack = NULL;
}

static void show_dir(const char *d, bool longform)
{
  const char *home = var_get("HOME");
  size_t hl = home != NULL ? strlen(home) : 0;

  if (!longform && hl > 0 && strncmp(d, home, hl) == 0 && (d[hl] == '\0' || d[hl] == '/'))
    {
      printf("~%s", d + hl);
    }
  else
    {
      fputs(d, stdout);
    }
}

/* Prints the stack: the working directory first, then the saved ones. */

static void show_stack(bool longform, bool per_line, bool numbered)
{
  char cwd[VS_PATH_MAX];
  int i;

  if (!cwd_of(cwd, sizeof(cwd)))
    {
      strcpy(cwd, ".");
    }

  for (i = 0; i <= g_sh.ndirs; i++)
    {
      if (numbered)
        {
          printf("%2d  ", i);
        }

      show_dir(i == 0 ? cwd : g_sh.dirstack[i - 1], longform);
      if (per_line || numbered)
        {
          putchar('\n');
        }
      else
        {
          putchar(i < g_sh.ndirs ? ' ' : '\n');
        }
    }
}

/* +N / -N as an index into "cwd, then the stack"; -1 if it is not one. */

static int stack_index(const char *arg)
{
  char *end;
  long n;

  if ((arg[0] != '+' && arg[0] != '-') || arg[1] == '\0')
    {
      return -1;
    }

  n = strtol(arg + 1, &end, 10);
  if (*end != '\0' || n < 0)
    {
      return -1;
    }

  return arg[0] == '+' ? (int)n : g_sh.ndirs - (int)n;
}

int bi_dirs(int argc, char **argv)
{
  bool longform = false;
  bool per_line = false;
  bool numbered = false;
  int i;

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "-c") == 0)
        {
          dirstack_free();
          return 0;
        }
      else if (strcmp(argv[i], "-l") == 0)
        {
          longform = true;
        }
      else if (strcmp(argv[i], "-p") == 0)
        {
          per_line = true;
        }
      else if (strcmp(argv[i], "-v") == 0)
        {
          numbered = true;
        }
      else
        {
          vs_err("dirs: %s: invalid option", argv[i]);
          return 2;
        }
    }

  show_stack(longform, per_line, numbered);
  return 0;
}

int bi_pushd(int argc, char **argv)
{
  char cwd[VS_PATH_MAX];
  bool no_cd = false;
  const char *arg = NULL;
  int i;

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "-n") == 0)
        {
          no_cd = true;
        }
      else if (arg == NULL)
        {
          arg = argv[i];
        }
      else
        {
          vs_err("pushd: too many arguments");
          return 2;
        }
    }

  if (!cwd_of(cwd, sizeof(cwd)))
    {
      vs_err("pushd: cannot determine the working directory");
      return 1;
    }

  if (arg == NULL)
    {
      /* no argument: swap the working directory with the top of the stack */

      char *top;

      if (g_sh.ndirs == 0)
        {
          vs_err("pushd: no other directory");
          return 1;
        }

      top = vs_xstrdup(g_sh.dirstack[0]);
      if (!no_cd && change_dir(top) != 0)
        {
          free(top);
          return 1;
        }

      if (!no_cd)
        {
          free(g_sh.dirstack[0]);
          g_sh.dirstack[0] = vs_xstrdup(cwd);
        }

      free(top);
      show_stack(false, false, false);
      return 0;
    }

  if (stack_index(arg) >= 0)
    {
      /* +N / -N: rotate so that entry N becomes the working directory */

      int idx = stack_index(arg);
      int total = g_sh.ndirs + 1;
      char **all;
      int k;

      if (idx >= total)
        {
          vs_err("pushd: %s: directory stack index out of range", arg);
          return 1;
        }

      all = vs_xmalloc((size_t)total * sizeof(char *));
      all[0] = vs_xstrdup(cwd);
      for (k = 1; k < total; k++)
        {
          all[k] = vs_xstrdup(g_sh.dirstack[k - 1]);
        }

      if (!no_cd && change_dir(all[idx]) != 0)
        {
          for (k = 0; k < total; k++)
            {
              free(all[k]);
            }

          free(all);
          return 1;
        }

      dirstack_free();
      g_sh.dirstack = vs_xmalloc((size_t)total * sizeof(char *));
      for (k = 1; k < total; k++)
        {
          g_sh.dirstack[k - 1] = all[(idx + k) % total];
        }

      g_sh.ndirs = total - 1;
      free(all[idx]);                 /* the others now belong to the stack */
      free(all);
      show_stack(false, false, false);
      return 0;
    }

  if (no_cd)
    {
      push_saved(arg);
    }
  else
    {
      if (change_dir(arg) != 0)
        {
          return 1;
        }

      push_saved(cwd);
    }

  show_stack(false, false, false);
  return 0;
}

int bi_popd(int argc, char **argv)
{
  bool no_cd = false;
  const char *arg = NULL;
  int i;
  int idx = 0;                        /* 0: the working directory */

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "-n") == 0)
        {
          no_cd = true;
        }
      else if (arg == NULL)
        {
          arg = argv[i];
        }
    }

  if (g_sh.ndirs == 0)
    {
      vs_err("popd: directory stack empty");
      return 1;
    }

  if (arg != NULL)
    {
      idx = stack_index(arg);
      if (idx < 0 || idx > g_sh.ndirs)
        {
          vs_err("popd: %s: directory stack index out of range", arg);
          return 1;
        }
    }

  if (idx == 0 && !no_cd)
    {
      if (change_dir(g_sh.dirstack[0]) != 0)
        {
          return 1;
        }

      drop_saved(0);
    }
  else if (idx == 0)
    {
      drop_saved(0);                  /* -n: forget the top without going there */
    }
  else
    {
      drop_saved(idx - 1);
    }

  show_stack(false, false, false);
  return 0;
}
