/*
 * complete.c -- tab completion candidates for the line editor (ledit.c). At
 * command position the
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

struct ent_s
{
  char *name;                   /* what gets listed */
  char *full;                   /* what the word becomes (name + '/' or ' ') */
};

struct cands_s
{
  struct ent_s *e;
  int cap;
  int n;
};

static void cands_free(struct cands_s *c)
{
  int i;

  for (i = 0; i < c->n; i++)
    {
      free(c->e[i].name);
      free(c->e[i].full);
    }

  free(c->e);
  c->e = NULL;
  c->n = c->cap = 0;
}

/* Add `name` (completing `prefix`) unless it is a duplicate or the table is
 * full. `suffix` is appended to the inserted text only (' ' or '/').
 */

static void cands_add(struct cands_s *c, const char *prefix, const char *name,
                      char suffix)
{
  size_t len = strlen(name);

  if (strncmp(name, prefix, strlen(prefix)) != 0)
    {
      return;
    }

  if (c->n == c->cap)
    {
      c->cap = c->cap > 0 ? c->cap * 2 : 32;
      c->e = vs_xrealloc(c->e, (size_t)c->cap * sizeof(c->e[0]));
    }

  c->e[c->n].name = strdup(name);
  c->e[c->n].full = malloc(len * 2 + 2);
  if (c->e[c->n].name == NULL || c->e[c->n].full == NULL)
    {
      free(c->e[c->n].name);
      free(c->e[c->n].full);
      return;
    }

  {
    char *d = c->e[c->n].full;
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
  return strcmp(((const struct ent_s *)a)->name, ((const struct ent_s *)b)->name);
}

/* Command names found on $PATH (and /bin), kept sorted in one array so a Tab
 * is a binary search. The cache is valid while no directory on the path has
 * changed: g_sig lists each directory with its modification time, and is
 * rebuilt and compared on every Tab (one stat() per directory, no readdir()).
 * Shared by every shell task on NuttX, which is harmless: it only caches what
 * the filesystem says, and a different PATH just rebuilds it.
 */

static char *g_sig;
static char **g_cmds;
static int g_ncmds;

static int cmp_str(const void *a, const void *b)
{
  return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Call fn(dir) for /bin and each directory of $PATH. */

static void each_path_dir(void (*fn)(const char *dir, void *arg), void *arg)
{
  const char *path = var_get("PATH");

  fn("/bin", arg);
  while (path != NULL && *path != '\0')
    {
      size_t len = strcspn(path, ":");
      char dir[VS_PATH_MAX];

      if (len > 0 && len < sizeof(dir))
        {
          memcpy(dir, path, len);
          dir[len] = '\0';
          fn(dir, arg);
        }

      path += len;
      if (*path == ':')
        {
          path++;
        }
    }
}

static void sig_add(const char *dir, void *arg)
{
  struct sbuf_s *sig = arg;
  struct stat st;
  char t[48];

  sb_adds(sig, dir);
  if (stat(dir, &st) == 0)
    {
      snprintf(t, sizeof(t), "=%lld.%ld;", (long long)st.st_mtim.tv_sec,
               (long)st.st_mtim.tv_nsec);
      sb_adds(sig, t);
    }

  sb_addc(sig, '\n');
}

static void scan_dir(const char *dir, void *arg)
{
  DIR *d = opendir(dir);
  struct dirent *e;

  (void)arg;
  if (d == NULL)
    {
      return;
    }

  while ((e = readdir(d)) != NULL)
    {
      if (e->d_name[0] != '.')
        {
          if (g_ncmds % 256 == 0)
            {
              g_cmds = vs_xrealloc(g_cmds, (size_t)(g_ncmds + 256) * sizeof(char *));
            }

          g_cmds[g_ncmds++] = vs_xstrdup(e->d_name);
        }
    }

  closedir(d);
}

static void refresh_commands(void)
{
  struct sbuf_s sig;
  int i;
  int u;

  sb_init(&sig);
  each_path_dir(sig_add, &sig);
  if (g_sig != NULL && sig.s != NULL && strcmp(g_sig, sig.s) == 0)
    {
      sb_free(&sig);
      return;
    }

  for (i = 0; i < g_ncmds; i++)
    {
      free(g_cmds[i]);
    }

  g_ncmds = 0;
  free(g_sig);
  g_sig = sig.s != NULL ? sb_take(&sig) : vs_xstrdup("");
  each_path_dir(scan_dir, NULL);
  qsort(g_cmds, (size_t)g_ncmds, sizeof(char *), cmp_str);
  for (i = 1, u = 0; i < g_ncmds; i++)          /* the same name in several directories */
    {
      if (strcmp(g_cmds[i], g_cmds[u]) == 0)
        {
          free(g_cmds[i]);
        }
      else
        {
          g_cmds[++u] = g_cmds[i];
        }
    }

  g_ncmds = g_ncmds > 0 ? u + 1 : 0;
}

static void complete_command(struct cands_s *c, const char *prefix)
{
  const struct builtin_s *b;
  size_t pl = strlen(prefix);
  int lo = 0;
  int hi;
  int i;

  for (b = g_vs_builtins; b->name != NULL; b++)
    {
      cands_add(c, prefix, b->name, ' ');
    }

  for (i = 0; g_tbx_commands[i] != NULL; i++)
    {
      cands_add(c, prefix, g_tbx_commands[i], ' ');
    }

  refresh_commands();
  hi = g_ncmds;
  while (lo < hi)                       /* first name >= prefix */
    {
      int mid = (lo + hi) / 2;

      if (strcmp(g_cmds[mid], prefix) < 0)
        {
          lo = mid + 1;
        }
      else
        {
          hi = mid;
        }
    }

  for (i = lo; i < g_ncmds && strncmp(g_cmds[i], prefix, pl) == 0; i++)
    {
      cands_add(c, prefix, g_cmds[i], ' ');
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

  while ((e = readdir(d)) != NULL)
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

struct spec_ctx_s
{
  struct cands_s *c;
  const char *word;
};

static void spec_cand(void *ctx, const char *name)
{
  struct spec_ctx_s *sc = ctx;

  cands_add(sc->c, sc->word, name, ' ');
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
  bool whole_word = false;
  int i;
  int u;
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

  if (word[0] == '$')
    {
      /* $NAME or ${NAME: the names of the shell's variables */

      bool brace = word[1] == '{';
      const struct var_s *v;
      char buf[160];

      for (v = g_sh.vars; v != NULL; v = v->next)
        {
          if (v->value != NULL || v->arr != NULL)
            {
              snprintf(buf, sizeof(buf), brace ? "${%s}" : "$%s", v->name);
              cands_add(&c, word, buf, brace ? '\0' : ' ');
            }
        }

      whole_word = true;                       /* the whole word is the candidate */
    }
  else if (is_cmd && strchr(word, '/') == NULL)
    {
      complete_command(&c, word);
    }
  else
    {
      if (!is_cmd)
        {
          /* The command this word belongs to: from after the last | ; & ( */

          const char *cs = prev;
          char cmd[128];
          size_t cl;

          while (cs > line && strchr("|;&(", cs[-1]) == NULL)
            {
              cs--;
            }

          while (*cs == ' ' || *cs == '\t')
            {
              cs++;
            }

          cl = strcspn(cs, " \t");
          if (cl > 0 && cl < sizeof(cmd))
            {
              struct spec_ctx_s sc = { &c, word };

              memcpy(cmd, cs, cl);
              cmd[cl] = '\0';
              if (vs_comp_from_spec(cmd, line, len, word, spec_cand, &sc) && c.n > 0)
                {
                  whole_word = true;
                }
            }
        }

      if (!whole_word)
        {
          complete_path(&c, word);
        }
    }

  n = c.n;
  if (n > 1)
    {
      qsort(c.e, n, sizeof(c.e[0]), cmp_names);
      for (i = 1, u = 0; i < n; i++)            /* the same name from several sources */
        {
          if (strcmp(c.e[i].name, c.e[u].name) == 0)
            {
              free(c.e[i].name);
              free(c.e[i].full);
            }
          else
            {
              c.e[++u] = c.e[i];
            }
        }

      c.n = n = u + 1;
    }

  if (n == 1)
    {
      /* full[0] still has the whole name; insert the part after the word */

      const char *base = strrchr(word, '/');
      size_t have = strlen(base != NULL && !is_cmd && !whole_word ? base + 1 : word);
      const char *full = c.e[0].full;

      if (strlen(full) >= have && have <= strlen(full))
        {
          snprintf(ins, (size_t)inslen, "%s", full + have);
        }
    }
  else if (n > 1)
    {
      /* Longest common prefix of the (unescaped) names */

      const char *base = strrchr(word, '/');
      size_t have = strlen(base != NULL && !is_cmd && !whole_word ? base + 1 : word);
      size_t common = strlen(c.e[0].name);

      for (i = 1; i < n; i++)
        {
          size_t k = 0;

          while (k < common && c.e[i].name[k] == c.e[0].name[k])
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
              if (c.e[0].name[k] == ' ')
                {
                  ins[o++] = '\\';
                }

              ins[o++] = c.e[0].name[k];
            }

          ins[o] = '\0';
        }

      for (i = 0; i < n; i++)
        {
          list(ctx, c.e[i].name);
        }
    }

  cands_free(&c);
  return n;
}

