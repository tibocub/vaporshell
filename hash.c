/*
 * hash.c -- the command hash: `hash` and the lookup cache behind it.
 *
 * A remembered location saves the PATH walk on every later use of a name.
 * Assigning PATH forgets everything (POSIX), and an entry whose file has
 * gone is looked up afresh. bash and dash agree on behaviour but not on how
 * `hash` prints (measured, docs/modes.md), hence VF_BASH_INFO_FORMATS.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vaporshell.h"
#include "exec.h"
#include "mode.h"
#include "platform.h"

static struct hash_s *hash_get(const char *name)
{
  struct hash_s *h;

  for (h = g_sh.hash; h != NULL; h = h->next)
    {
      if (strcmp(h->name, name) == 0)
        {
          return h;
        }
    }

  return NULL;
}

static void hash_put(const char *name, const char *path, int hits)
{
  struct hash_s *h = hash_get(name);

  if (h == NULL)
    {
      h = vs_xmalloc(sizeof(*h));
      h->name = vs_xstrdup(name);
      h->path = NULL;
      h->next = g_sh.hash;
      g_sh.hash = h;
    }

  free(h->path);
  h->path = vs_xstrdup(path);
  h->hits = hits;
}

static void hash_drop(const char *name)
{
  struct hash_s **pp;

  for (pp = &g_sh.hash; *pp != NULL; pp = &(*pp)->next)
    {
      if (strcmp((*pp)->name, name) == 0)
        {
          struct hash_s *h = *pp;

          *pp = h->next;
          free(h->name);
          free(h->path);
          free(h);
          return;
        }
    }
}

void hash_clear(void)
{
  while (g_sh.hash != NULL)
    {
      hash_drop(g_sh.hash->name);
    }
}

/* Command lookup for execution: cached for plain names. */

char *hash_find_command(const char *name, const char *path_var, int *err)
{
  struct hash_s *h;
  char *path;

  if (strchr(name, '/') != NULL)
    {
      return vs_plat_find_command(name, path_var, err);
    }

  h = hash_get(name);
  if (h != NULL)
    {
      if (access(h->path, X_OK) == 0)
        {
          h->hits++;
          return vs_xstrdup(h->path);
        }

      hash_drop(name);
    }

  path = vs_plat_find_command(name, path_var, err);
  if (path != NULL && strchr(path, '/') != NULL)
    {
      hash_put(name, path, 1);
    }

  return path;
}

int bi_hash(int argc, char **argv)
{
  int i = 1;
  int status = 0;
  const char *ppath = NULL;
  bool cleared = false;
  bool bash = vs_feat(VF_BASH_INFO_FORMATS);

  for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++)
    {
      if (strcmp(argv[i], "--") == 0)
        {
          i++;
          break;
        }
      else if (strcmp(argv[i], "-r") == 0)
        {
          hash_clear();
          cleared = true;
        }
      else if (bash && strcmp(argv[i], "-p") == 0 && i + 1 < argc)
        {
          ppath = argv[++i];
        }
      else if (bash && strcmp(argv[i], "-d") == 0)
        {
          for (i++; i < argc; i++)
            {
              if (hash_get(argv[i]) == NULL)
                {
                  vs_err("hash: %s: not found", argv[i]);
                  status = 1;
                }
              else
                {
                  hash_drop(argv[i]);
                }
            }

          return status;
        }
      else
        {
          vs_err("hash: %s: invalid option", argv[i]);
          return 2;
        }
    }

  if (ppath != NULL)
    {
      if (i >= argc)
        {
          vs_err("hash: -p: name required");
          return 1;
        }

      hash_put(argv[i], ppath, 0);
      return 0;
    }

  if (i >= argc && cleared)
    {
      return 0;
    }

  if (i >= argc)
    {
      struct hash_s *h;

      if (g_sh.hash == NULL)
        {
          if (bash)
            {
              puts("hash: hash table empty");
            }

          return 0;
        }

      if (bash)
        {
          puts("hits\tcommand");
        }

      for (h = g_sh.hash; h != NULL; h = h->next)
        {
          if (bash)
            {
              printf("%4d\t%s\n", h->hits, h->path);
            }
          else
            {
              puts(h->path);
            }
        }

      return 0;
    }

  for (; i < argc; i++)
    {
      int err = 0;
      char *path;

      hash_drop(argv[i]);
      path = vs_plat_find_command(argv[i], var_get("PATH"), &err);
      if (path == NULL)
        {
          vs_err("hash: %s: not found", argv[i]);
          status = 1;
          continue;
        }

      if (strchr(path, '/') != NULL)
        {
          hash_put(argv[i], path, 0);
        }

      free(path);
    }

  return status;
}
