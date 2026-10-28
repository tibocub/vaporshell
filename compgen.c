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

#ifdef VAPORSHELL_POSIX
#  define _XOPEN_SOURCE 700              /* getpwent() */
#endif

#include <nuttx/config.h>
#include <dirent.h>
#include <pwd.h>
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
#include "platform.h"
#include "complete.h"
#include "rcfile.h"
#include "compfish.h"
#ifndef VAPORSHELL_POSIX                /* the options of the tbx commands: there are none on a host */
#  include "comp_defaults.h"
#endif

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

/* When set, candidates go here instead of to stdout (vash_comp_from_spec). */

/* compopt -o nospace, from the completion function that just ran (see vash_comp_nospace) */

static bool g_comp_running;
static bool g_comp_nospace;
static bool g_comp_filenames;

bool vash_comp_nospace(void)
{
  return g_comp_nospace;
}

bool vash_comp_filenames(void)
{
  return g_comp_filenames;
}

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

struct emit_s
{
  struct cg_s *c;
  const char *pre;
  const char *suf;
};

static void emit_name(const char *name, void *arg)
{
  struct emit_s *em = arg;

  emit(em->c, name, em->pre, em->suf);
}

/* -W: the list is split at IFS (quotes protect), each word is expanded
 * (parameters, $(...), arithmetic), and what an unquoted word expands to is
 * split at IFS again -- compgen -W '$words' works as in bash.
 * shortcut: a word holding any quote or backslash is kept whole after
 * expansion; bash would still split its unquoted parts.
 */

static void gen_wordlist(struct cg_s *c, const char *words, const char *pre, const char *suf)
{
  const char *ifs = var_get("IFS") != NULL ? var_get("IFS") : " \t\n";
  size_t n = strlen(words);
  size_t i = 0;

  while (i < n)
    {
      size_t start;
      bool quoted = false;
      char q = '\0';
      char *tok;
      char *val;

      while (i < n && strchr(ifs, words[i]) != NULL && words[i] != '\0')
        {
          i++;
        }

      start = i;
      for (; i < n; i++)
        {
          if (q != '\0')
            {
              if (words[i] == q)
                {
                  q = '\0';
                }
              else if (q == '"' && words[i] == '\\' && i + 1 < n)
                {
                  i++;
                }
            }
          else if (words[i] == '\'' || words[i] == '"')
            {
              q = words[i];
              quoted = true;
            }
          else if (words[i] == '\\' && i + 1 < n)
            {
              i++;
              quoted = true;
            }
          else if (words[i] == '$' && i + 1 < n && (words[i + 1] == '(' || words[i + 1] == '{'))
            {
              char open = words[i + 1];
              char close = open == '(' ? ')' : '}';
              int depth = 0;

              for (i++; i < n; i++)             /* $(...) and ${...} are part of the word */
                {
                  depth += (words[i] == open) - (words[i] == close);
                  if (depth == 0)
                    {
                      break;
                    }
                }
            }
          else if (strchr(ifs, words[i]) != NULL)
            {
              break;
            }
        }

      if (i == start)
        {
          continue;
        }

      tok = vash_xstrndup(words + start, i - start);
      val = expand_word_str(tok);
      free(tok);
      if (val == NULL)
        {
          continue;
        }

      if (quoted)
        {
          emit(c, val, pre, suf);
        }
      else
        {
          char *p = val;

          while (*p != '\0')
            {
              char *e;

              while (*p != '\0' && strchr(ifs, *p) != NULL)
                {
                  p++;
                }

              e = p;
              while (*e != '\0' && strchr(ifs, *e) == NULL)
                {
                  e++;
                }

              if (e > p)
                {
                  char *w = vash_xstrndup(p, (size_t)(e - p));

                  emit(c, w, pre, suf);
                  free(w);
                }

              p = e;
            }
        }

      free(val);
    }
}

/* -f (any entry) / -d (directories only): the current directory (or the
 * directory part of the prefix, as bash also honours: "compgen -f -- sub/f"
 * lists inside sub/).
 */

static void gen_dir_entries(struct cg_s *c, bool dirs_only, const char *pre, const char *suf)
{
  const char *slash = strrchr(c->pfx, '/');
  char *dirpart = slash != NULL ? vash_xstrndup(c->pfx, (size_t)(slash - c->pfx) + 1)
                                : vash_xstrdup("");
  const char *leaf = slash != NULL ? slash + 1 : c->pfx;
  size_t leaflen = strlen(leaf);
  char fbuf[VASH_PATH_MAX];
  DIR *d;
  struct dirent *de;

  /* NuttX's opendir() rejects "." and "./" in a path: the platform gives one it
   * accepts, while entries are still printed with the original 'dirpart'.
   */

  d = opendir(vash_plat_fspath(dirpart[0] != '\0' ? dirpart : ".", fbuf, sizeof(fbuf)));
  if (d == NULL)
    {
      free(dirpart);
      return;
    }

  while ((de = readdir(d)) != NULL)
    {
      char full[1024];
      bool isdir;

      if (strncmp(de->d_name, leaf, leaflen) != 0 || strcmp(de->d_name, ".") == 0 ||
          strcmp(de->d_name, "..") == 0)
        {
          continue;
        }

      snprintf(full, sizeof(full), "%s%s", dirpart, de->d_name);
      isdir = (dirs_only || g_sink != NULL) && vash_dirent_isdir(de, full);
      if (dirs_only && !isdir)
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
        if (isdir && g_sink != NULL)
          {
            sb_addc(&name, '/');          /* only for completion: tells the editor it is a directory */
          }

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
  struct emit_s em = { c, pre, suf };
  const struct builtin_s *b;
  struct func_s *f;

  for (b = g_vash_builtins; b->name != NULL; b++)
    {
      if ((b->modes & vash_mode_bit()) != 0 && builtin_is_enabled(b->name))
        {
          emit(c, b->name, pre, suf);
        }
    }

  for (f = g_sh.funcs; f != NULL; f = f->next)
    {
      emit(c, f->name, pre, suf);
    }

  vash_each_path_command(emit_name, &em);
}

static void emit_job(const char *spec, const char *cmd, void *arg)
{
  (void)cmd;
  emit_name(spec, arg);
}

/* Directories under each CDPATH entry, for a name with no '/' in it. */

static void gen_cdpath(struct cg_s *c, const char *pre, const char *suf)
{
  const char *cdpath = var_get("CDPATH");

  while (cdpath != NULL && *cdpath != '\0' && strchr(c->pfx, '/') == NULL)
    {
      size_t n = strcspn(cdpath, ":");
      char dir[VASH_PATH_MAX];

      if (n > 0 && n < sizeof(dir) - 2)
        {
          char fbuf[VASH_PATH_MAX];
          DIR *d;
          struct dirent *de;

          memcpy(dir, cdpath, n);
          dir[n] = '\0';
          d = opendir(vash_plat_fspath(dir, fbuf, sizeof(fbuf)));
          while (d != NULL && (de = readdir(d)) != NULL)
            {
              struct stat st;
              char full[VASH_PATH_MAX];

              if (de->d_name[0] != '.' && snprintf(full, sizeof(full), "%s/%s", dir, de->d_name) < (int)sizeof(full) &&
                  stat(vash_plat_fspath(full, fbuf, sizeof(fbuf)), &st) == 0 && S_ISDIR(st.st_mode))
                {
                  emit(c, de->d_name, pre, suf);
                }
            }

          if (d != NULL)
            {
              closedir(d);
            }
        }

      cdpath += n + (cdpath[n] == ':');
    }
}

/* fn(name, arg) for each user in the password database. Not built on NuttX, where
 * getpwent() needs CONFIG_LIBC_PASSWD_FILE and there is no user database yet.
 */

void vash_each_user(void (*fn)(const char *name, void *arg), void *arg)
{
#ifdef VAPORSHELL_POSIX
  struct passwd *pw;

  setpwent();
  while ((pw = getpwent()) != NULL)
    {
      fn(pw->pw_name, arg);
    }

  endpwent();
#else
  (void)fn;
  (void)arg;
#endif
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

      for (b = g_vash_builtins; b->name != NULL; b++)
        {
          if ((b->modes & vash_mode_bit()) != 0 && builtin_is_enabled(b->name))
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
  else if (strcmp(action, "job") == 0)
    {
      vash_each_job(emit_job, &(struct emit_s){ c, pre, suf });
    }
  else if (strcmp(action, "user") == 0)
    {
      vash_each_user(emit_name, &(struct emit_s){ c, pre, suf });
    }
  else if (strcmp(action, "cdpath") == 0)
    {
      gen_cdpath(c, pre, suf);
    }
  else if (strcmp(action, "signal") == 0)
    {
      const char *n;
      char dashed[16];
      int i;

      for (i = 0; c->pfx[0] == '-' && (n = vash_signal_name(i)) != NULL; i++)   /* only for kill -<Tab> */
        {
          snprintf(dashed, sizeof(dashed), "-%s", n);
          emit(c, dashed, pre, suf);
        }
    }
  else
    {
      vash_err("compgen: %s: invalid action name", action);
      c->n = -1;
    }
}

/* compgen -V: the candidates go into an array variable, as quoted words of name=(...) */

static void sink_to_assign(void *ctx, const char *word, const char *desc)
{
  struct sbuf_s *b = ctx;

  (void)desc;
  sb_adds(b, " '");
  for (; *word != '\0'; word++)
    {
      if (*word == '\'')
        {
          sb_adds(b, "'\\''");
        }
      else
        {
          sb_addc(b, *word);
        }
    }

  sb_addc(b, '\'');
}

int bi_compgen(int argc, char **argv)
{
  struct cg_s c;
  const char *into = NULL;
  struct sbuf_s collected;
  void (*saved_sink)(void *, const char *, const char *) = g_sink;
  void *saved_ctx = g_sink_ctx;
  int rc;
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
      else if (strcmp(a, "-V") == 0 && i + 1 < argc)
        {
          into = argv[++i];
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
          vash_err("compgen: %s: invalid option", a);
          return 2;
        }
      else
        {
          word = a;
        }
    }

  c.pfx = word;
  c.pfxlen = strlen(word);

  sb_init(&collected);
  if (into != NULL)
    {
      g_sink = sink_to_assign;
      g_sink_ctx = &collected;
    }

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

  g_sink = saved_sink;
  g_sink_ctx = saved_ctx;
  if (into != NULL)
    {
      struct sbuf_s asg;

      sb_init(&asg);
      sb_adds(&asg, into);
      sb_adds(&asg, "=(");
      sb_adds(&asg, collected.s != NULL ? collected.s : "");
      sb_adds(&asg, " )");
      assign_apply(asg.s);
      sb_free(&asg);
    }

  sb_free(&collected);
  if (c.n < 0)
    {
      return 2;
    }

  rc = c.hits > 0 ? 0 : 1;
  return rc;
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

/* `complete -p` prints in bash's fixed order: -o options, single-letter actions,
 * -A, -G, -W, -P, -S, -X, -C, -F; the string arguments quoted.
 */

static int split_opts(const char *o, char **v, int max);

static void print_spec(const struct complete_spec_s *s)
{
  static const char *const oorder[] = { "bashdefault", "default", "dirnames", "filenames",
                                        "noquote", "nosort", "nospace", "plusdirs" };
  char *v[48];
  int n = split_opts(s->opts, v, 48);
  int i;
  int k;
  const char *letters = "abcdefgjksuv";
  const char *argopts = "AGWPSXCF";
  const char *p;

  printf("complete");
  for (k = 0; k < (int)(sizeof(oorder) / sizeof(oorder[0])); k++)
    {
      for (i = 0; i + 1 < n; i++)
        {
          if (strcmp(v[i], "-o") == 0 && strcmp(v[i + 1], oorder[k]) == 0)
            {
              printf(" -o %s", oorder[k]);
            }
        }
    }

  for (p = letters; *p != '\0'; p++)
    {
      for (i = 0; i < n; i++)
        {
          if (v[i][0] == '-' && v[i][1] == *p && v[i][2] == '\0')
            {
              printf(" -%c", *p);
            }
        }
    }

  for (p = argopts; *p != '\0'; p++)
    {
      for (i = 0; i + 1 < n; i++)
        {
          if (v[i][0] == '-' && v[i][1] == *p && v[i][2] == '\0')
            {
              if (*p == 'A' || *p == 'F')
                {
                  printf(" -%c %s", *p, v[i + 1]);
                }
              else
                {
                  printf(" -%c '%s'", *p, v[i + 1]);
                }
            }
        }
    }

  printf(" %s\n", s->name);
  for (i = 0; i < n; i++)
    {
      free(v[i]);
    }
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
  int fish = vash_complete_fish(argc, argv);
  bool list = false;
  bool remove = false;
  struct sbuf_s opts;
  int i = 1;
  int nnames = 0;
  const char *special = NULL;         /* -D / -E / -I stand in for the names */
  char *one[2] = { NULL, NULL };

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
      else if (strcmp(a, "-D") == 0 || strcmp(a, "-E") == 0 || strcmp(a, "-I") == 0)
        {
          special = a;
        }
      else if (strcmp(a, "--") == 0)
        {
          i++;                                /* the names follow, whatever they start with */
          break;
        }
      else if (a[0] == '-' && a[1] != '\0')
        {
          sb_adds(&opts, a);
          sb_addc(&opts, ' ');

          /* only these take an argument; -a -b -c -j -u -v ... do not */

          if (a[2] == '\0' && strchr("AWFCGoPSX", a[1]) != NULL && i + 1 < argc)
            {
              const char *arg = argv[i + 1];
              bool plain = *arg != '\0' && strspn(arg, "abcdefghijklmnopqrstuvwxyz"
                                                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
                                                      "_+,./:=@%-") == strlen(arg);

              if (plain)                      /* bash prints a plain word as it is */
                {
                  sb_adds(&opts, arg);
                  sb_addc(&opts, ' ');
                }
              else
                {
                  sb_adds(&opts, "'");
                  sb_adds(&opts, arg);
                  sb_adds(&opts, "' ");
                }

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

  if (special != NULL && i >= argc)
    {
      one[1] = (char *)special;           /* the one name */
      argv = one;
      argc = 2;
      i = 1;
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
                  vash_err("complete: %s: no completion specification", argv[i]);
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
          s = vash_xmalloc(sizeof(*s));
          s->name = vash_xstrdup(argv[i]);
          s->next = g_sh.complete_specs;
          g_sh.complete_specs = s;
        }
      else
        {
          free(s->opts);
        }

      s->opts = vash_xstrdup(opts.s != NULL ? opts.s : "");
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
  int i;

  if (!g_comp_running)
    {
      vash_err("compopt: not currently executing completion function");
      return 1;
    }

  for (i = 1; i + 1 < argc && (argv[i][0] == '-' || argv[i][0] == '+'); i += 2)
    {
      if (argv[i][1] == 'o' && strcmp(argv[i + 1], "nospace") == 0)
        {
          g_comp_nospace = argv[i][0] == '-';
        }
      else if (argv[i][1] == 'o' && strcmp(argv[i + 1], "filenames") == 0)
        {
          g_comp_filenames = argv[i][0] == '-';
        }
    }

  return 0;
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

      v[n++] = b.s != NULL ? sb_take(&b) : vash_xstrdup("");
    }

  return n;
}

/* bash's COMP_WORDS / COMP_CWORD / COMP_LINE / COMP_POINT for a complete -F function. */

void vash_set_comp_vars(const char *line, int point)
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
        char *w = vash_xmalloc((size_t)(i - start) + 1);

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
  var_set("COMP_TYPE", "9");                /* TAB: normal completion */
  var_set("COMP_KEY", "9");
  snprintf(num, sizeof(num), "%d", cword);
  var_set("COMP_CWORD", num);
  var_set("COMP_LINE", line);
  snprintf(num, sizeof(num), "%d", point);
  var_set("COMP_POINT", num);
}

/* Candidates for 'word' (the last word of line[0..point)) from the spec
 * registered for 'cmd'. False if there is none. A -F function is the slow
 * lane: it runs in this shell, stopped after $VASH_COMPLETION_TIMEOUT ms (default 300; complete.c).
 */

/* What the shell's own commands complete to, when the user has not said
 * otherwise. Plain data: nothing is registered or loaded at startup.
 */

static const struct complete_spec_s g_default_specs[] =
{
  { NULL, "cd", "-d -A cdpath " }, { NULL, "pushd", "-d -A cdpath " },
  { NULL, "fg", "-A job " },   { NULL, "bg", "-A job " },
  { NULL, "disown", "-A job " }, { NULL, "wait", "-A job " },
  { NULL, "kill", "-A signal -A job " },
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
 * <dir>/<cmd>.vash from $VASH_COMPLETION_PATH (default
 * ~/.config/vaporshell/completions:/etc/vaporshell/completions), run once the
 * first time the command is completed; else the shell's own defaults.
 */

static const struct complete_spec_s *spec_for(const char *cmd)
{
  const struct complete_spec_s *s = find_spec(cmd);
  const char *dirs = var_get("VASH_COMPLETION_PATH");
  const char *home = var_get("HOME");
  struct sbuf_s dflt;
  size_t i;

  if (s != NULL || vash_fish_known(cmd))
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
      sb_adds(&file, ".vash");
      if (vash_rc_source(file.s))
        {
          s = find_spec(cmd);
          if (s == NULL && !vash_fish_known(cmd))
            {
              vash_fish_mark(cmd);                /* sourced: do not run it again */
            }
        }

      sb_free(&file);
      if (s != NULL || vash_fish_known(cmd))
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

/* Run one spec: compgen with its options, then its -F function. The
 * function's exit status (-1: there is none), for 124 = "retry" below.
 */

static int run_spec(const struct complete_spec_s *spec, const char *cmd, const char *line,
                    int point, const char *word, vash_comp_list_f sink, void *ctx)
{
  char *ov[48];
  char *av[52];
  const char *func = NULL;
  int status = -1;
  int n;
  int i;

  g_comp_filenames = strstr(spec->opts, "filenames") != NULL;
  g_comp_nospace = strstr(spec->opts, "nospace") != NULL;
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

      vash_set_comp_vars(line, point);
      var_unset("COMPREPLY");
      g_comp_nospace = false;
      g_comp_running = true;
      fa[0] = (char *)func;
      fa[1] = (char *)cmd;
      fa[2] = (char *)word;
      fa[3] = (char *)"";
      status = run_argv(4, fa, false);
      g_comp_running = false;
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

  return status;
}

int vash_comp_from_spec(const char *cmd, const char *line, int point, const char *word,
                      vash_comp_list_f sink, void *ctx)
{
  const struct complete_spec_s *spec = spec_for(cmd);
  int found = vash_comp_fish(cmd, line, point, word, sink, ctx);

  if (spec == NULL && spec_for("-D") != NULL)
    {
      /* complete -D: the spec for commands that have none. Its function may
       * load one (bash-completion does) and answer 124: look again.
       */

      if (run_spec(spec_for("-D"), cmd, line, point, word, sink, ctx) == 124)
        {
          spec = spec_for(cmd);
        }
      else
        {
          return found | VASH_SPEC_FOUND;
        }
    }

  if (spec == NULL)
    {
      /* A word that starts with '-' on a command whose options are known
       * (comp_defaults.h): files are still offered after anything else.
       */

#ifndef VAPORSHELL_POSIX
      if (found == 0 && word[0] == '-')
        {
          int i;

          for (i = 0; i < (int)(sizeof(g_comp_defaults) / sizeof(g_comp_defaults[0])); i++)
            {
              const char *o;
              char opt[40];

              if (strcmp(g_comp_defaults[i].cmd, cmd) != 0)
                {
                  continue;
                }

              for (o = g_comp_defaults[i].shorts; *o != '\0'; o++)
                {
                  opt[0] = '-';
                  opt[1] = *o;
                  opt[2] = '\0';
                  sink(ctx, opt, NULL);
                }

              for (o = g_comp_defaults[i].longs; *o != '\0'; o += strcspn(o, " ") + (o[strcspn(o, " ")] == ' '))
                {
                  size_t n = strcspn(o, " ");

                  if (n + 3 < sizeof(opt))
                    {
                      opt[0] = opt[1] = '-';
                      memcpy(opt + 2, o, n);
                      opt[n + 2] = '\0';
                      sink(ctx, opt, NULL);
                    }
                }

              return VASH_SPEC_FOUND;
            }
        }
#endif

      return found;
    }

  run_spec(spec, cmd, line, point, word, sink, ctx);
  return found | VASH_SPEC_FOUND;
}
