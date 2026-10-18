/*
 * compgen.c -- compgen: generate a list of possible completions.
 *
 * Genuinely useful outside interactive tab-completion (a script can call it
 * directly to build a word list), unlike `complete`/`compopt`/`bind`, which
 * only make sense with a line editor to drive -- this shell has none, so
 * those are accepted-but-inert elsewhere. compgen's own ordering follows
 * whatever the underlying source gives (directory order for -f/-d, PATH
 * order for -A command, the order -W was given): bash does not sort these
 * either, so this is not an approximation, just not alphabetical.
 */

#include <nuttx/config.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "exec.h"
#include "expand.h"
#include "mode.h"
#include "complete.h"
#include "rcfile.h"
#include "compfish.h"

static const char *const KEYWORDS[] =
{
  "if", "then", "else", "elif", "fi", "case", "esac", "for", "select",
  "while", "until", "do", "done", "in", "function", "time", "{", "}", "!",
  "[[", "]]", NULL
};

struct cg_s
{
  const char *pfx;
  size_t pfxlen;
  const char *glob_x;         /* -X: exclude matches of this pattern */
  int n;
  int hits;
};

/* When set, candidates go here instead of to stdout (vs_comp_from_spec). */

static void (*g_sink)(void *ctx, const char *word, const char *desc);
static void *g_sink_ctx;

/* One candidate, a+b+c (any may be NULL): to the sink if set, else a line on stdout. */

static void out3(const char *a, const char *b, const char *c)
{
  if (g_sink != NULL)
    {
      struct sbuf_s w;

      sb_init(&w);
      sb_adds(&w, a != NULL ? a : "");
      sb_adds(&w, b);
      sb_adds(&w, c != NULL ? c : "");
      g_sink(g_sink_ctx, w.s, NULL);
      sb_free(&w);
    }
  else
    {
      printf("%s%s%s\n", a != NULL ? a : "", b, c != NULL ? c : "");
    }
}

static bool has_prefix(const struct cg_s *c, const char *s)
{
  return strncmp(s, c->pfx, c->pfxlen) == 0;
}

static void emit(struct cg_s *c, const char *s, const char *prefix_opt, const char *suffix_opt)
{
  if (!has_prefix(c, s))
    {
      return;
    }

  if (c->glob_x != NULL && pat_match(c->glob_x, NULL, strlen(c->glob_x), s))
    {
      return;
    }

  out3(prefix_opt, s, suffix_opt);
  c->hits++;
}

static void gen_wordlist(struct cg_s *c, const char *words, const char *pre, const char *suf)
{
  const char *ifs = " \t\n";
  char *copy = vs_xstrdup(words);
  char *tok = strtok(copy, ifs);

  while (tok != NULL)
    {
      emit(c, tok, pre, suf);
      tok = strtok(NULL, ifs);
    }

  free(copy);
}

/* -f (any entry) / -d (directories only): the current directory (or the
 * directory part of the prefix, as bash also honours: "compgen -f -- sub/f"
 * lists inside sub/).
 */

static void gen_dir_entries(struct cg_s *c, bool dirs_only, const char *pre, const char *suf)
{
  const char *slash = strrchr(c->pfx, '/');
  char *dirpart = slash != NULL ? vs_xstrndup(c->pfx, (size_t)(slash - c->pfx) + 1)
                                : vs_xstrdup("");
  const char *leaf = slash != NULL ? slash + 1 : c->pfx;
  size_t leaflen = strlen(leaf);
  const char *norm;            /* dirpart with a leading "./" (however many) peeled off */
  char *open_path;
  DIR *d;
  struct dirent *de;

  /* NuttX's opendir() (and even its own `ls`) rejects "." and "./" wherever
   * they appear in a path, absolute or not; resolve to a plain absolute
   * path with any leading "./" removed to open, while still printing
   * entries with the original, unmodified 'dirpart' prefix bash would.
   */

  norm = dirpart;
  while (norm[0] == '.' && norm[1] == '/')
    {
      norm += 2;
    }

  if (norm[0] == '/')
    {
      open_path = vs_xstrdup(norm);
    }
  else
    {
      char cwd[1024];

      if (getcwd(cwd, sizeof(cwd)) != NULL)
        {
          if (norm[0] == '\0')
            {
              open_path = vs_xstrdup(cwd);
            }
          else
            {
              open_path = vs_xmalloc(strlen(cwd) + 1 + strlen(norm) + 1);
              sprintf(open_path, "%s/%s", cwd, norm);
            }
        }
      else
        {
          open_path = vs_xstrdup(dirpart);
        }
    }

  d = opendir(open_path);
  free(open_path);
  if (d == NULL)
    {
      free(dirpart);
      return;
    }

  while ((de = readdir(d)) != NULL)
    {
      char full[1024];
      struct stat st;

      if (strncmp(de->d_name, leaf, leaflen) != 0 || strcmp(de->d_name, ".") == 0 ||
          strcmp(de->d_name, "..") == 0)
        {
          continue;
        }

      snprintf(full, sizeof(full), "%s%s", dirpart, de->d_name);
      if (dirs_only && (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)))
        {
          continue;
        }

      if (c->glob_x != NULL && pat_match(c->glob_x, NULL, strlen(c->glob_x), de->d_name))
        {
          continue;
        }

      {
        struct sbuf_s name;

        sb_init(&name);
        sb_adds(&name, dirpart);
        sb_adds(&name, de->d_name);
        out3(pre, name.s, suf);
        sb_free(&name);
      }

      c->hits++;
    }

  closedir(d);
  free(dirpart);
}

/* -A command: an external program on PATH; here also includes the builtins
 * and functions, since a plain command word can resolve to any of them.
 */

static void gen_commands(struct cg_s *c, const char *pre, const char *suf)
{
  const char *path = var_get("PATH");
  char *copy;
  char *dir;
  const struct builtin_s *b;
  struct func_s *f;

  for (b = g_vs_builtins; b->name != NULL; b++)
    {
      if ((b->modes & vs_mode_bit()) != 0 && builtin_is_enabled(b->name))
        {
          emit(c, b->name, pre, suf);
        }
    }

  for (f = g_sh.funcs; f != NULL; f = f->next)
    {
      emit(c, f->name, pre, suf);
    }

  if (path == NULL || path[0] == '\0')
    {
      return;
    }

  copy = vs_xstrdup(path);
  dir = strtok(copy, ":");
  while (dir != NULL)
    {
      DIR *d = opendir(dir[0] != '\0' ? dir : ".");
      struct dirent *de;

      if (d != NULL)
        {
          while ((de = readdir(d)) != NULL)
            {
              if (de->d_name[0] != '.')
                {
                  emit(c, de->d_name, pre, suf);
                }
            }

          closedir(d);
        }

      dir = strtok(NULL, ":");
    }

  free(copy);
}

static void gen_action(struct cg_s *c, const char *action, const char *pre, const char *suf)
{
  if (strcmp(action, "variable") == 0)
    {
      struct var_s *v;

      for (v = g_sh.vars; v != NULL; v = v->next)
        {
          emit(c, v->name, pre, suf);
        }
    }
  else if (strcmp(action, "function") == 0)
    {
      struct func_s *f;

      for (f = g_sh.funcs; f != NULL; f = f->next)
        {
          emit(c, f->name, pre, suf);
        }
    }
  else if (strcmp(action, "builtin") == 0)
    {
      const struct builtin_s *b;

      for (b = g_vs_builtins; b->name != NULL; b++)
        {
          if ((b->modes & vs_mode_bit()) != 0 && builtin_is_enabled(b->name))
            {
              emit(c, b->name, pre, suf);
            }
        }
    }
  else if (strcmp(action, "alias") == 0)
    {
      struct alias_s *a;

      for (a = g_sh.aliases; a != NULL; a = a->next)
        {
          emit(c, a->name, pre, suf);
        }
    }
  else if (strcmp(action, "keyword") == 0)
    {
      int i;

      for (i = 0; KEYWORDS[i] != NULL; i++)
        {
          emit(c, KEYWORDS[i], pre, suf);
        }
    }
  else if (strcmp(action, "command") == 0)
    {
      gen_commands(c, pre, suf);
    }
  else if (strcmp(action, "file") == 0)
    {
      gen_dir_entries(c, false, pre, suf);
    }
  else if (strcmp(action, "directory") == 0)
    {
      gen_dir_entries(c, true, pre, suf);
    }
  else
    {
      vs_err("compgen: %s: invalid action name", action);
      c->n = -1;
    }
}

int bi_compgen(int argc, char **argv)
{
  struct cg_s c;
  const char *wordlist = NULL;
  const char *pre = NULL;
  const char *suf = NULL;
  const char *word = "";
  bool want_f = false;
  bool want_d = false;
  int actions_n = 0;
  const char *actions[16];
  int i;

  c.pfx = "";
  c.pfxlen = 0;
  c.glob_x = NULL;
  c.n = 0;
  c.hits = 0;

  for (i = 1; i < argc; i++)
    {
      const char *a = argv[i];

      if (strcmp(a, "--") == 0)
        {
          i++;
          if (i < argc)
            {
              word = argv[i];
            }

          break;
        }

      if (strcmp(a, "-W") == 0 && i + 1 < argc)
        {
          wordlist = argv[++i];
        }
      else if (strcmp(a, "-P") == 0 && i + 1 < argc)
        {
          pre = argv[++i];
        }
      else if (strcmp(a, "-S") == 0 && i + 1 < argc)
        {
          suf = argv[++i];
        }
      else if (strcmp(a, "-X") == 0 && i + 1 < argc)
        {
          c.glob_x = argv[++i];
        }
      else if (strcmp(a, "-A") == 0 && i + 1 < argc)
        {
          if (actions_n < (int)(sizeof(actions) / sizeof(actions[0])))
            {
              actions[actions_n++] = argv[++i];
            }
          else
            {
              i++;
            }
        }
      else if (strcmp(a, "-v") == 0)
        {
          if (actions_n < (int)(sizeof(actions) / sizeof(actions[0])))
            {
              actions[actions_n++] = "variable";
            }
        }
      else if (strlen(a) == 2 && a[0] == '-' && strchr("abck", a[1]) != NULL)
        {
          /* the single-letter shorthands for the common -A actions */

          static const char letters[] = "abck";
          static const char *const names[] = { "alias", "builtin", "command", "keyword" };
          const char *hit = strchr(letters, a[1]);

          if (actions_n < (int)(sizeof(actions) / sizeof(actions[0])))
            {
              actions[actions_n++] = names[hit - letters];
            }
        }
      else if (strcmp(a, "-f") == 0)
        {
          want_f = true;
        }
      else if (strcmp(a, "-d") == 0)
        {
          want_d = true;
        }
      else if (strcmp(a, "-o") == 0 || strcmp(a, "-G") == 0 || strcmp(a, "-C") == 0 ||
               strcmp(a, "-F") == 0)
        {
          if (i + 1 < argc)
            {
              i++;                  /* accepted, no completion-mode effect here */
            }
        }
      else if (a[0] == '-')
        {
          vs_err("compgen: %s: invalid option", a);
          return 2;
        }
      else
        {
          word = a;
        }
    }

  c.pfx = word;
  c.pfxlen = strlen(word);

  if (wordlist != NULL)
    {
      gen_wordlist(&c, wordlist, pre, suf);
    }

  if (want_f)
    {
      gen_dir_entries(&c, false, pre, suf);
    }

  if (want_d)
    {
      gen_dir_entries(&c, true, pre, suf);
    }

  for (i = 0; i < actions_n && c.n >= 0; i++)
    {
      gen_action(&c, actions[i], pre, suf);
    }

  if (c.n < 0)
    {
      return 2;
    }

  return c.hits > 0 ? 0 : 1;
}

/* ---- complete / compopt --------------------------------------------------------- */

static struct complete_spec_s *find_spec(const char *name)
{
  struct complete_spec_s *s;

  for (s = g_sh.complete_specs; s != NULL; s = s->next)
    {
      if (strcmp(s->name, name) == 0)
        {
          return s;
        }
    }

  return NULL;
}

static void print_spec(const struct complete_spec_s *s)
{
  printf("complete %s%s\n", s->opts, s->name);
}

/* complete [-F func|-C cmd|-W wordlist|-G glob|-A action] [-o opt]... name...
 * -p [name...]: print (all specs, or these) in a form `complete` accepts back
 * -r [name...]: remove
 *
 * There is no line editor to drive interactively here, so a registered spec
 * has no effect beyond existing for `complete -p`/`-r` to see; compgen is
 * this shell's real way to generate a word list from a script.
 */

int bi_complete(int argc, char **argv)
{
  int fish = vs_complete_fish(argc, argv);
  bool list = false;
  bool remove = false;
  struct sbuf_s opts;
  int i = 1;
  int nnames = 0;

  if (fish >= 0)
    {
      return fish;
    }

  sb_init(&opts);
  for (; i < argc; i++)
    {
      const char *a = argv[i];

      if (strcmp(a, "-p") == 0)
        {
          list = true;
        }
      else if (strcmp(a, "-r") == 0)
        {
          remove = true;
        }
      else if (a[0] == '-' && a[1] != '\0')
        {
          sb_adds(&opts, a);
          sb_addc(&opts, ' ');
          if (i + 1 < argc && argv[i + 1][0] != '-')
            {
              sb_adds(&opts, "'");
              sb_adds(&opts, argv[i + 1]);
              sb_adds(&opts, "' ");
              i++;
            }
        }
      else
        {
          break;
        }
    }

  if (remove)
    {
      for (; i < argc; i++)
        {
          struct complete_spec_s **p = &g_sh.complete_specs;

          while (*p != NULL && strcmp((*p)->name, argv[i]) != 0)
            {
              p = &(*p)->next;
            }

          if (*p != NULL)
            {
              struct complete_spec_s *dead = *p;

              *p = dead->next;
              free(dead->name);
              free(dead->opts);
              free(dead);
            }
        }

      sb_free(&opts);
      return 0;
    }

  if (list || i >= argc)
    {
      struct complete_spec_s *s;

      if (i < argc)
        {
          for (; i < argc; i++)
            {
              s = find_spec(argv[i]);
              if (s != NULL)
                {
                  print_spec(s);
                }
              else
                {
                  vs_err("complete: %s: no completion specification", argv[i]);
                  nnames = -1;
                }
            }
        }
      else
        {
          for (s = g_sh.complete_specs; s != NULL; s = s->next)
            {
              print_spec(s);
            }
        }

      sb_free(&opts);
      return nnames < 0 ? 1 : 0;
    }

  for (; i < argc; i++)
    {
      struct complete_spec_s *s = find_spec(argv[i]);

      if (s == NULL)
        {
          s = vs_xmalloc(sizeof(*s));
          s->name = vs_xstrdup(argv[i]);
          s->next = g_sh.complete_specs;
          g_sh.complete_specs = s;
        }
      else
        {
          free(s->opts);
        }

      s->opts = vs_xstrdup(opts.s != NULL ? opts.s : "");
      nnames++;
    }

  sb_free(&opts);
  return nnames > 0 ? 0 : 1;
}

/* Only meaningful while a completion function bash itself invoked is
 * running, which never happens here (there is no interactive completion to
 * drive it) -- so, like bash outside that context, this always fails.
 */

int bi_compopt(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  vs_err("compopt: not currently executing completion function");
  return 1;
}

/* ---- Completion from a registered spec ------------------------------------------ */

/* Split a stored option string ("-W 'a b' -f ") back into words. */

static int split_opts(const char *o, char **v, int max)
{
  int n = 0;

  while (*o != '\0' && n < max)
    {
      struct sbuf_s b;

      while (*o == ' ')
        {
          o++;
        }

      if (*o == '\0')
        {
          break;
        }

      sb_init(&b);
      if (*o == '\'')
        {
          for (o++; *o != '\0' && *o != '\''; o++)
            {
              sb_addc(&b, *o);
            }

          if (*o == '\'')
            {
              o++;
            }
        }
      else
        {
          for (; *o != '\0' && *o != ' '; o++)
            {
              sb_addc(&b, *o);
            }
        }

      v[n++] = b.s != NULL ? sb_take(&b) : vs_xstrdup("");
    }

  return n;
}

/* bash's COMP_WORDS / COMP_CWORD / COMP_LINE / COMP_POINT for a complete -F function. */

void vs_set_comp_vars(const char *line, int point)
{
  struct arr_s *words = arr_new();
  int i = 0;
  int cword = 0;
  char num[16];

  while (i < point)
    {
      int start;

      while (i < point && (line[i] == ' ' || line[i] == '\t'))
        {
          i++;
        }

      if (i >= point)
        {
          break;
        }

      start = i;
      while (i < point && line[i] != ' ' && line[i] != '\t')
        {
          i++;
        }

      {
        char *w = vs_xmalloc((size_t)(i - start) + 1);

        memcpy(w, line + start, (size_t)(i - start));
        w[i - start] = '\0';
        arr_append(words, w);
        free(w);
      }
    }

  cword = (int)(arr_max_index(words) + 1);
  if (point > 0 && cword > 0 && line[point - 1] != ' ' && line[point - 1] != '\t')
    {
      cword--;                              /* still typing the last word */
    }
  else
    {
      arr_append(words, "");                /* a fresh, empty word */
    }

  var_array_replace("COMP_WORDS", words);
  snprintf(num, sizeof(num), "%d", cword);
  var_set("COMP_CWORD", num);
  var_set("COMP_LINE", line);
  snprintf(num, sizeof(num), "%d", point);
  var_set("COMP_POINT", num);
}

/* Candidates for 'word' (the last word of line[0..point)) from the spec
 * registered for 'cmd'. False if there is none. A -F function is the slow
 * lane: it runs in this shell, uncapped (shortcut: no time budget yet).
 */

/* What the shell's own commands complete to, when the user has not said
 * otherwise. Plain data: nothing is registered or loaded at startup.
 */

static const struct complete_spec_s g_default_specs[] =
{
  { NULL, "cd", "-d " },       { NULL, "pushd", "-d " },
  { NULL, "unset", "-v " },    { NULL, "export", "-v " },
  { NULL, "readonly", "-v " }, { NULL, "declare", "-v " },
  { NULL, "typeset", "-v " },  { NULL, "local", "-v " },
  { NULL, "unalias", "-a " },  { NULL, "type", "-c " },
  { NULL, "command", "-c " },  { NULL, "which", "-c " },
  { NULL, "builtin", "-b " },  { NULL, "help", "-b " },
  { NULL, "enable", "-b " },   { NULL, "source", "-f " },
  { NULL, ".", "-f " },
};

/* The spec for 'cmd': one registered with `complete`; else a file
 * <dir>/<cmd>.vsh from $VS_COMPLETION_PATH (default
 * ~/.config/vaporshell/completions:/etc/vaporshell/completions), run once the
 * first time the command is completed; else the shell's own defaults.
 */

static const struct complete_spec_s *spec_for(const char *cmd)
{
  const struct complete_spec_s *s = find_spec(cmd);
  const char *dirs = var_get("VS_COMPLETION_PATH");
  const char *home = var_get("HOME");
  struct sbuf_s dflt;
  size_t i;

  if (s != NULL || vs_fish_known(cmd))
    {
      return s;
    }

  sb_init(&dflt);
  if (dirs == NULL)
    {
      if (home != NULL && *home != '\0')
        {
          sb_adds(&dflt, home);
          sb_adds(&dflt, "/.config/vaporshell/completions:");
        }

      sb_adds(&dflt, "/etc/vaporshell/completions");
      dirs = dflt.s;
    }

  while (strchr(cmd, '/') == NULL && dirs != NULL && *dirs != '\0')
    {
      size_t len = strcspn(dirs, ":");
      struct sbuf_s file;

      sb_init(&file);
      sb_addn(&file, dirs, len);
      sb_addc(&file, '/');
      sb_adds(&file, cmd);
      sb_adds(&file, ".vsh");
      if (vs_rc_source(file.s))
        {
          s = find_spec(cmd);
          if (s == NULL && !vs_fish_known(cmd))
            {
              vs_fish_mark(cmd);                /* sourced: do not run it again */
            }
        }

      sb_free(&file);
      if (s != NULL || vs_fish_known(cmd))
        {
          sb_free(&dflt);
          return s;
        }

      dirs += len + (dirs[len] == ':' ? 1 : 0);
    }

  sb_free(&dflt);
  for (i = 0; i < sizeof(g_default_specs) / sizeof(g_default_specs[0]); i++)
    {
      if (strcmp(g_default_specs[i].name, cmd) == 0)
        {
          return &g_default_specs[i];
        }
    }

  return NULL;
}

int vs_comp_from_spec(const char *cmd, const char *line, int point, const char *word,
                      vs_comp_list_f sink, void *ctx)
{
  const struct complete_spec_s *spec = spec_for(cmd);
  char *ov[48];
  char *av[52];
  const char *func = NULL;
  int found = vs_comp_fish(cmd, line, point, word, sink, ctx);
  int n;
  int i;

  if (spec == NULL)
    {
      return found;
    }

  n = split_opts(spec->opts, ov, 48);
  av[0] = "compgen";
  for (i = 0; i < n; i++)
    {
      av[i + 1] = ov[i];
      if (strcmp(ov[i], "-F") == 0 && i + 1 < n)
        {
          func = ov[i + 1];
        }
    }

  av[n + 1] = "--";
  av[n + 2] = (char *)word;
  g_sink = sink;
  g_sink_ctx = ctx;
  bi_compgen(n + 3, av);
  g_sink = NULL;

  if (func != NULL && func_find(func) != NULL)
    {
      char *fa[4];
      const struct arr_s *r;
      long k;

      vs_set_comp_vars(line, point);
      var_unset("COMPREPLY");
      fa[0] = (char *)func;
      fa[1] = (char *)cmd;
      fa[2] = (char *)word;
      fa[3] = (char *)"";
      run_argv(4, fa, false);
      r = var_array("COMPREPLY", false);
      for (k = 0; r != NULL && k <= arr_max_index(r); k++)
        {
          const char *e = arr_get(r, k);

          if (e != NULL)
            {
              sink(ctx, e, NULL);
            }
        }
    }

  for (i = 0; i < n; i++)
    {
      free(ov[i]);
    }

  return found | VS_SPEC_FOUND;
}
