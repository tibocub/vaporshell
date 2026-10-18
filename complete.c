/*
 * complete.c -- interactive tab completion for the NuttX readline front end.
 *
 * Registered with readline_completer() (a vaporOS patch to NuttX's
 * system/readline, see vaporOS/patches/nuttx-apps). At command position the
 * word is completed against shell builtins, tbx applets and the executables
 * in /bin and $PATH; anywhere else it is completed as a file name, with a
 * trailing '/' on directories. Quoting is only minimally understood (a
 * leading quote is skipped, spaces in inserted names are backslash-escaped).
 */

#include <nuttx/config.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "complete.h"

#include "vaporshell.h"
#include "exec.h"
#include "platform.h"

#ifdef VAPORSHELL_POSIX
static const char *const g_tbx_commands[] = { NULL };     /* no multicall table here */
#else
extern const char *const g_tbx_commands[];      /* dispatch.c */
#endif

#define MAX_CANDS 64

struct cands_s
{
  char *name[MAX_CANDS];        /* what gets listed */
  char *full[MAX_CANDS];        /* what the word becomes (name + '/' or ' ') */
  int n;
};

static void cands_free(struct cands_s *c)
{
  int i;

  for (i = 0; i < c->n; i++)
    {
      free(c->name[i]);
      free(c->full[i]);
    }

  c->n = 0;
}

/* Add `name` (completing `prefix`) unless it is a duplicate or the table is
 * full. `suffix` is appended to the inserted text only (' ' or '/').
 */

static void cands_add(struct cands_s *c, const char *prefix, const char *name,
                      char suffix)
{
  size_t len = strlen(name);
  int i;

  if (strncmp(name, prefix, strlen(prefix)) != 0 || c->n >= MAX_CANDS)
    {
      return;
    }

  for (i = 0; i < c->n; i++)
    {
      if (strcmp(c->name[i], name) == 0)
        {
          return;
        }
    }

  c->name[c->n] = strdup(name);
  c->full[c->n] = malloc(len * 2 + 2);
  if (c->name[c->n] == NULL || c->full[c->n] == NULL)
    {
      free(c->name[c->n]);
      free(c->full[c->n]);
      return;
    }

  {
    char *d = c->full[c->n];
    const char *s;

    for (s = name; *s != '\0'; s++)
      {
        if (*s == ' ')
          {
            *d++ = '\\';
          }

        *d++ = *s;
      }

    if (suffix != '\0')
      {
        *d++ = suffix;
      }

    *d = '\0';
  }

  c->n++;
}

static int cmp_names(const void *a, const void *b)
{
  return strcmp(*(char *const *)a, *(char *const *)b);
}

static void add_dir_commands(struct cands_s *c, const char *dir,
                             const char *prefix)
{
  DIR *d = opendir(dir);
  struct dirent *e;

  if (d == NULL)
    {
      return;
    }

  while ((e = readdir(d)) != NULL && c->n < MAX_CANDS)
    {
      if (e->d_name[0] != '.')
        {
          cands_add(c, prefix, e->d_name, ' ');
        }
    }

  closedir(d);
}

static void complete_command(struct cands_s *c, const char *prefix)
{
  const char *path = var_get("PATH");
  const struct builtin_s *b;
  int i;

  for (b = g_vs_builtins; b->name != NULL; b++)
    {
      cands_add(c, prefix, b->name, ' ');
    }

  for (i = 0; g_tbx_commands[i] != NULL; i++)
    {
      cands_add(c, prefix, g_tbx_commands[i], ' ');
    }

  add_dir_commands(c, "/bin", prefix);

  while (path != NULL && *path != '\0' && c->n < MAX_CANDS)
    {
      size_t len = strcspn(path, ":");
      char dir[VS_PATH_MAX];

      if (len > 0 && len < sizeof(dir))
        {
          memcpy(dir, path, len);
          dir[len] = '\0';
          add_dir_commands(c, dir, prefix);
        }

      path += len;
      if (*path == ':')
        {
          path++;
        }
    }
}

static void complete_path(struct cands_s *c, const char *word)
{
  const char *slash = strrchr(word, '/');
  const char *base = slash != NULL ? slash + 1 : word;
  char dir[VS_PATH_MAX];
  char fbuf[VS_PATH_MAX];
  char child[VS_PATH_MAX];
  DIR *d;
  struct dirent *e;
  struct stat st;
  size_t dlen = slash != NULL ? (size_t)(slash - word) + 1 : 0;

  if (dlen >= sizeof(dir))
    {
      return;
    }

  memcpy(dir, word, dlen);
  dir[dlen] = '\0';

  d = opendir(dlen > 0 ? vs_plat_fspath(dir, fbuf, sizeof(fbuf)) : ".");
  if (d == NULL)
    {
      return;
    }

  while ((e = readdir(d)) != NULL && c->n < MAX_CANDS)
    {
      char suffix = ' ';

      if (e->d_name[0] == '.' && base[0] != '.')
        {
          continue;
        }

      if (strncmp(e->d_name, base, strlen(base)) != 0)
        {
          continue;
        }

      if (dlen + strlen(e->d_name) + 1 < sizeof(child))
        {
          memcpy(child, dir, dlen);
          strcpy(child + dlen, e->d_name);
          if (stat(dlen > 0 ? vs_plat_fspath(child, fbuf, sizeof(fbuf))
                            : e->d_name, &st) == 0 && S_ISDIR(st.st_mode))
            {
              suffix = '/';
            }
        }

      cands_add(c, base, e->d_name, suffix);
    }

  closedir(d);
}

int vs_complete(const char *line, int len, char *ins, int inslen,
                vs_comp_list_f list, void *ctx)
{
  struct cands_s c;
  char word[VS_PATH_MAX];
  const char *w = line + len;
  const char *prev;
  size_t wlen;
  bool is_cmd;
  int i;
  int n;

  memset(&c, 0, sizeof(c));
  ins[0] = '\0';

  /* The word under the cursor: back to the last unescaped space. */

  while (w > line && !(w[-1] == ' ' && (w - 1 == line || w[-2] != '\\')) &&
         w[-1] != '\t')
    {
      w--;
    }

  if (*w == '"' || *w == '\'')
    {
      w++;
    }

  wlen = (size_t)(line + len - w);
  if (wlen >= sizeof(word))
    {
      return 0;
    }

  /* Unescape "\ " while copying */

  {
    size_t o = 0;
    size_t k;

    for (k = 0; k < wlen; k++)
      {
        if (w[k] == '\\' && k + 1 < wlen && w[k + 1] == ' ')
          {
            continue;
          }

        word[o++] = w[k];
      }

    word[o] = '\0';
    wlen = o;
  }

  /* Command position: first word, or right after | ; & ( */

  prev = w;
  while (prev > line && (prev[-1] == ' ' || prev[-1] == '\t'))
    {
      prev--;
    }

  is_cmd = prev == line || strchr("|;&(", prev[-1]) != NULL;

  if (is_cmd && strchr(word, '/') == NULL)
    {
      complete_command(&c, word);
    }
  else
    {
      complete_path(&c, word);
    }

  n = c.n;
  if (n > 1)
    {
      qsort(c.name, n, sizeof(c.name[0]), cmp_names);
    }

  if (n == 1)
    {
      /* full[0] still has the whole name; insert the part after the word */

      const char *base = strrchr(word, '/');
      size_t have = strlen(base != NULL && !is_cmd ? base + 1 : word);
      const char *full = c.full[0];

      if (strlen(full) >= have && have <= strlen(full))
        {
          snprintf(ins, (size_t)inslen, "%s", full + have);
        }
    }
  else if (n > 1)
    {
      /* Longest common prefix of the (unescaped) names */

      const char *base = strrchr(word, '/');
      size_t have = strlen(base != NULL && !is_cmd ? base + 1 : word);
      size_t common = strlen(c.name[0]);

      for (i = 1; i < n; i++)
        {
          size_t k = 0;

          while (k < common && c.name[i][k] == c.name[0][k])
            {
              k++;
            }

          common = k;
        }

      if (common > have && common - have < (size_t)inslen - 1)
        {
          size_t o = 0;
          size_t k;

          for (k = have; k < common; k++)
            {
              if (c.name[0][k] == ' ')
                {
                  ins[o++] = '\\';
                }

              ins[o++] = c.name[0][k];
            }

          ins[o] = '\0';
        }

      for (i = 0; i < n; i++)
        {
          list(ctx, c.name[i]);
        }
    }

  cands_free(&c);
  return n;
}

