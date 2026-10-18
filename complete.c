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
#include <strings.h>
#include <sys/stat.h>

#include "complete.h"
#include "parse.h"

#include "vaporshell.h"
#include "exec.h"
#include "platform.h"

#ifdef VAPORSHELL_POSIX
static const char *const g_tbx_commands[] = { NULL };     /* no multicall table here */
#  define is_tbx_command(name) false
#else
bool is_tbx_command(const char *name);          /* dispatch.c */
extern const char *const g_tbx_commands[];      /* dispatch.c */
#endif

struct ent_s
{
  char *name;                   /* what gets listed */
  char *full;                   /* what the word becomes (name + '/' or ' ') */
  char *desc;                   /* one line about it, or NULL */
};

struct cands_s
{
  int mode;                     /* 0 prefix, 1 prefix ignoring case, 2 anywhere in the name, ignoring case */
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
      free(c->e[i].desc);
    }

  free(c->e);
  c->e = NULL;
  c->n = c->cap = 0;
}

/* Add `name` (completing `prefix`) unless it is a duplicate or the table is
 * full. `suffix` is appended to the inserted text only (' ' or '/').
 */

/* Does 'name' match what was typed, the way the current mode says? */

static bool matches(int mode, const char *name, const char *typed)
{
  size_t tl = strlen(typed);

  if (mode == 0)
    {
      return strncmp(name, typed, tl) == 0;
    }

  if (mode == 1)
    {
      return strncasecmp(name, typed, tl) == 0;
    }

  for (; *name != '\0'; name++)
    {
      if (strncasecmp(name, typed, tl) == 0)
        {
          return true;
        }
    }

  return tl == 0;
}

static void cands_add(struct cands_s *c, const char *prefix, const char *name,
                      char suffix, const char *desc)
{
  size_t len = strlen(name);

  if (!matches(c->mode, name, prefix))
    {
      return;
    }

  if (c->n == c->cap)
    {
      c->cap = c->cap > 0 ? c->cap * 2 : 32;
      c->e = vs_xrealloc(c->e, (size_t)c->cap * sizeof(c->e[0]));
    }

  c->e[c->n].name = strdup(name);
  c->e[c->n].desc = desc != NULL ? strdup(desc) : NULL;
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

/* Call fn for every name in the (refreshed) $PATH table, sorted, once each. */

void vs_each_path_command(void (*fn)(const char *name, void *arg), void *arg)
{
  int i;

  refresh_commands();
  for (i = 0; i < g_ncmds; i++)
    {
      fn(g_cmds[i], arg);
    }
}

static void complete_command(struct cands_s *c, const char *prefix)
{
  const struct builtin_s *b;
  const struct alias_s *a;
  const struct func_s *f;
  size_t pl = strlen(prefix);
  int lo = 0;
  int hi;
  int i;

  for (b = g_vs_builtins; b->name != NULL; b++)
    {
      cands_add(c, prefix, b->name, ' ', NULL);
    }

  for (a = g_sh.aliases; a != NULL; a = a->next)
    {
      cands_add(c, prefix, a->name, ' ', NULL);
    }

  for (f = g_sh.funcs; f != NULL; f = f->next)
    {
      cands_add(c, prefix, f->name, ' ', NULL);
    }

  for (i = 0; g_tbx_commands[i] != NULL; i++)
    {
      cands_add(c, prefix, g_tbx_commands[i], ' ', NULL);
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
      cands_add(c, prefix, g_cmds[i], ' ', NULL);
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
  if (strcmp(word, "~") == 0)
    {
      cands_add(c, word, "~", '/', NULL);
      return;
    }

  if (dir[0] == '~' && dir[1] == '/' && var_get("HOME") != NULL)    /* only the lookup is expanded */
    {
      char home[VS_PATH_MAX];

      if (snprintf(home, sizeof(home), "%s%s", var_get("HOME"), dir + 1) >= (int)sizeof(home))
        {
          return;
        }

      strcpy(dir, home);
      dlen = strlen(dir);
    }

  d = opendir(vs_plat_fspath(dlen > 0 ? dir : ".", fbuf, sizeof(fbuf)));    /* NuttX rejects a bare "." */
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

      if (!matches(c->mode, e->d_name, base))
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

      cands_add(c, base, e->d_name, suffix, NULL);
    }

  closedir(d);
}

struct spec_ctx_s
{
  struct cands_s *c;
  const char *word;
};

static void spec_cand(void *ctx, const char *name, const char *desc)
{
  struct spec_ctx_s *sc = ctx;
  char fbuf[VS_PATH_MAX];
  struct stat st;
  bool dir = name[0] != '-' && name[0] != '$' &&
             stat(vs_plat_fspath(name, fbuf, sizeof(fbuf)), &st) == 0 && S_ISDIR(st.st_mode);

  cands_add(sc->c, sc->word, name, dir ? '/' : ' ', desc);
}

/* What the generators need to know about the word being completed. */

struct gen_s
{
  const char *line;
  int len;
  const char *word;             /* unescaped */
  const char *prev;             /* start of the blanks before the word */
  bool is_cmd;                  /* in command position */
  bool whole_word;              /* candidates stand for the whole word (variables, specs) */
  bool by_spec;                 /* a spec answered: no looser matching */
};

static void generate(struct cands_s *c, struct gen_s *g)
{
  const char *word = g->word;

  g->whole_word = false;
  g->by_spec = false;
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
              cands_add(c, word, buf, brace ? '\0' : ' ', NULL);
            }
        }

      g->whole_word = true;
    }
  else if (g->is_cmd && strchr(word, '/') == NULL)
    {
      complete_command(c, word);
    }
  else
    {
      if (!g->is_cmd && c->mode == 0)
        {
          /* The command this word belongs to: from after the last | ; & ( */

          const char *cs = g->prev;
          char cmd[128];
          size_t cl;

          while (cs > g->line && strchr("|;&(", cs[-1]) == NULL)
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
              struct spec_ctx_s sc = { c, word };
              const char *lim = var_get("VS_COMPLETION_TIMEOUT");
              long ms = lim != NULL ? strtol(lim, NULL, 10) : 300;
              int r;

              memcpy(cmd, cs, cl);
              cmd[cl] = '\0';
              int status = g_sh.last_status;
              int pipest[64];
              int npipest = g_sh.nps;

              memcpy(pipest, g_sh.ps, sizeof(pipest));
              g_sh.comp_expired = false;
              g_sh.comp_deadline = ms > 0 ? vs_now_ms() + ms : 0;
              r = vs_comp_from_spec(cmd, g->line, g->len, word, spec_cand, &sc);
              g_sh.comp_deadline = 0;
              g_sh.last_status = status;               /* spec files and -F functions must not change $? */
              g_sh.nps = npipest;
              memcpy(g_sh.ps, pipest, sizeof(pipest));
              if (g_sh.comp_expired)
                {
                  g_sh.unwind = UW_NONE;         /* what was running is abandoned; the candidates so far stay */
                }

              if ((r & VS_SPEC_FOUND) != 0 && (c->n > 0 || (r & VS_SPEC_EXCLUSIVE) != 0))
                {
                  g->whole_word = true;
                  g->by_spec = true;
                }
            }
        }

      if (!g->whole_word)
        {
          complete_path(c, word);
        }
    }
}

/* Complete the word before the cursor. If nothing starts with it, names that
 * start with it in another case are tried, then names that contain it (in either
 * case): those replace what was typed, which is *del bytes before the cursor.
 */

int vs_complete(const char *line, int len, char *ins, int inslen, int *del,
                vs_comp_cand_f list, void *ctx)
{
  struct cands_s c;
  struct gen_s g;
  size_t have;
  char word[VS_PATH_MAX];
  const char *w = line + len;
  size_t wlen;
  size_t base_off;
  int i;
  int u;
  int n;

  memset(&c, 0, sizeof(c));
  ins[0] = '\0';
  *del = 0;

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

  g.prev = w;
  while (g.prev > line && (g.prev[-1] == ' ' || g.prev[-1] == '\t'))
    {
      g.prev--;
    }

  g.line = line;
  g.len = len;
  g.word = word;
  g.is_cmd = g.prev == line || strchr("|;&(", g.prev[-1]) != NULL;

  for (c.mode = 0; c.mode < 3; c.mode++)
    {
      generate(&c, &g);
      if (c.n > 0 || g.by_spec)
        {
          break;
        }

      cands_free(&c);
    }

  if (c.mode == 3)
    {
      c.mode = 2;
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
              free(c.e[i].desc);
            }
          else
            {
              c.e[++u] = c.e[i];
            }
        }

      c.n = n = u + 1;
    }

  /* How much of the word the names are matched against: all of it, or what
   * follows the last '/' when a path is completed.
   */

  {
    const char *slash = strrchr(word, '/');

    base_off = slash != NULL && !g.whole_word ? (size_t)(slash + 1 - word) : 0;
    have = wlen - base_off;
  }

  if (c.mode > 0 && n > 0)
    {
      /* Looser matches replace the typed text: count its raw bytes (a "\ "
       * is two) back from the cursor.
       */

      size_t left = have;
      const char *r = line + len;

      while (left > 0 && r > line)
        {
          r -= (r[-1] == ' ' && r - 1 > line && r[-2] == '\\') ? 2 : 1;
          left--;
        }

      *del = (int)(line + len - r);
      have = 0;
    }

  if (n == 1)
    {
      /* full[0] still has the whole name; insert the part after the word */

      const char *full = c.e[0].full;

      if (strlen(full) >= have)
        {
          snprintf(ins, (size_t)inslen, "%s", full + have);
        }
    }
  else if (n > 1)
    {
      /* Longest common prefix of the (unescaped) names, when they all start with the word */

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

      if (c.mode == 0 && common > have && common - have < (size_t)inslen - 1)
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
          list(ctx, c.e[i].name, strlen(c.e[i].full) >= have ? c.e[i].full + have : "", c.e[i].desc);
        }
    }

  cands_free(&c);
  return n;
}

void vs_commands_refresh(void)
{
  refresh_commands();
}

/* Does 'name' run something: a keyword, alias, function, builtin, a program in
 * the cached $PATH table, or a path to an executable file? For highlighting.
 */

bool vs_is_command(const char *name)
{
  struct stat st;
  int lo = 0;
  int hi;

  if (vs_parser_reserved_word(name) || alias_find(name) != NULL ||
      func_find(name) != NULL || builtin_find(name) != NULL || is_tbx_command(name))
    {
      return true;
    }

  if (strchr(name, '/') != NULL)
    {
      return stat(VS_FS(name), &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0111) != 0;
    }

  hi = g_ncmds;                         /* the table was refreshed by vs_commands_refresh() */
  while (lo < hi)
    {
      int mid = (lo + hi) / 2;
      int r = strcmp(g_cmds[mid], name);

      if (r == 0)
        {
          return true;
        }

      if (r < 0)
        {
          lo = mid + 1;
        }
      else
        {
          hi = mid;
        }
    }

  return false;
}
