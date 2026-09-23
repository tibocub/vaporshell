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

  printf("%s%s%s\n", prefix_opt != NULL ? prefix_opt : "", s,
         suffix_opt != NULL ? suffix_opt : "");
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

      printf("%s%s%s%s%s\n", pre != NULL ? pre : "", dirpart, de->d_name,
             (dirs_only && S_ISDIR(st.st_mode)) ? "" : "", suf != NULL ? suf : "");
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
  bool list = false;
  bool remove = false;
  struct sbuf_s opts;
  int i = 1;
  int nnames = 0;

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
