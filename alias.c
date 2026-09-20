/*
 * alias.c -- the alias table, `alias` and `unalias`. Expansion itself is
 * done by the parser (try_alias in parser.c), because POSIX defines it in
 * terms of re-reading the alias text as input.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "exec.h"
#include "mode.h"

const struct alias_s *alias_find(const char *name)
{
  struct alias_s *a;

  for (a = g_sh.aliases; a != NULL; a = a->next)
    {
      if (strcmp(a->name, name) == 0)
        {
          return a;
        }
    }

  return NULL;
}

static void alias_set(const char *name, const char *value)
{
  struct alias_s *a;

  for (a = g_sh.aliases; a != NULL; a = a->next)
    {
      if (strcmp(a->name, name) == 0)
        {
          free(a->value);
          a->value = vs_xstrdup(value);
          return;
        }
    }

  a = vs_xmalloc(sizeof(*a));
  a->name = vs_xstrdup(name);
  a->value = vs_xstrdup(value);
  a->next = g_sh.aliases;
  g_sh.aliases = a;
}

static bool alias_remove(const char *name)
{
  struct alias_s **pp;

  for (pp = &g_sh.aliases; *pp != NULL; pp = &(*pp)->next)
    {
      if (strcmp((*pp)->name, name) == 0)
        {
          struct alias_s *a = *pp;

          *pp = a->next;
          free(a->name);
          free(a->value);
          free(a);
          return true;
        }
    }

  return false;
}

void aliases_free(void)
{
  while (g_sh.aliases != NULL)
    {
      alias_remove(g_sh.aliases->name);
    }
}

/* name='value', quoting embedded single quotes the way sh does. */

static void print_alias(const struct alias_s *a)
{
  const char *p;

  if (vs_feat(VF_BASH_INFO_FORMATS))
    {
      fputs("alias ", stdout);
    }

  printf("%s='", a->name);
  for (p = a->value; *p != '\0'; p++)
    {
      if (*p == '\'')
        {
          fputs("'\\''", stdout);
        }
      else
        {
          putchar(*p);
        }
    }

  puts("'");
}

/* dash lists in hash-table order: 39 buckets keyed by
 * (first char << 4) + sum of chars, oldest first inside a bucket. bash
 * sorts by name. Both are visible to scripts that print `alias`.
 */

static unsigned dash_bucket(const char *name)
{
  unsigned h = ((unsigned char)name[0]) << 4;
  const char *p;

  for (p = name; *p != '\0'; p++)
    {
      h += (unsigned char)*p;
    }

  return h % 39;
}

static int cmp_name(const void *a, const void *b)
{
  return strcmp((*(const struct alias_s *const *)a)->name,
                (*(const struct alias_s *const *)b)->name);
}

static void print_all(void)
{
  const struct alias_s **v;
  const struct alias_s *a;
  size_t n = 0;
  size_t i;

  for (a = g_sh.aliases; a != NULL; a = a->next)
    {
      n++;
    }

  if (n == 0)
    {
      return;
    }

  v = vs_xmalloc(n * sizeof(*v));
  i = n;
  for (a = g_sh.aliases; a != NULL; a = a->next)
    {
      v[--i] = a;                      /* the list is newest-first: reverse it */
    }

  if (vs_feat(VF_BASH_INFO_FORMATS))
    {
      qsort(v, n, sizeof(*v), cmp_name);
      for (i = 0; i < n; i++)
        {
          print_alias(v[i]);
        }
    }
  else
    {
      unsigned bucket;

      for (bucket = 0; bucket < 39; bucket++)
        {
          for (i = 0; i < n; i++)
            {
              if (dash_bucket(v[i]->name) == bucket)
                {
                  print_alias(v[i]);
                }
            }
        }
    }

  free(v);
}

int bi_alias(int argc, char **argv)
{
  int status = 0;
  int i = 1;

  if (i < argc && strcmp(argv[i], "-p") == 0)
    {
      i++;
    }

  if (i >= argc)
    {
      print_all();
      return 0;
    }

  for (; i < argc; i++)
    {
      const char *eq = strchr(argv[i], '=');

      if (eq == NULL)
        {
          const struct alias_s *a = alias_find(argv[i]);

          if (a == NULL)
            {
              vs_err("alias: %s: not found", argv[i]);
              status = 1;
            }
          else
            {
              print_alias(a);
            }
        }
      else
        {
          char *name = vs_xstrndup(argv[i], (size_t)(eq - argv[i]));

          alias_set(name, eq + 1);
          free(name);
        }
    }

  return status;
}

int bi_unalias(int argc, char **argv)
{
  int status = 0;
  int i = 1;

  if (i < argc && strcmp(argv[i], "-a") == 0)
    {
      aliases_free();
      return 0;
    }

  if (argc < 2)
    {
      vs_err("unalias: usage: unalias [-a] name [name ...]");
      return 2;
    }

  for (; i < argc; i++)
    {
      if (!alias_remove(argv[i]))
        {
          vs_err("unalias: %s: not found", argv[i]);
          status = 1;
        }
    }

  return status;
}
