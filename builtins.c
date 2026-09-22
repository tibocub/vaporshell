/*
 * builtins.c -- builtin commands. One table drives dispatch, `help`,
 * `type` and `command -v`, so nothing has to be listed twice. Builtins
 * that only make sense inside the shell process (cd, export, set, ...)
 * live here; anything a program can do on its own stays external.
 *
 * Mode note: entries marked special are POSIX special builtins (their
 * assignments persist and errors are fatal in a non-interactive shell).
 * Bash-only builtins will be added to the same table with a mode flag.
 */

#include <nuttx/config.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "vaporshell.h"
#include "expand.h"
#include "exec.h"
#include "platform.h"

/* ---- Small helpers -------------------------------------------------------------- */

static bool parse_count(const char *s, int *out)
{
  char *end;
  long v = strtol(s, &end, 10);

  if (*s == '\0' || *end != '\0' || v < 0 || v > INT_MAX)
    {
      return false;
    }

  *out = (int)v;
  return true;
}

/* Prints name='value' with embedded quotes escaped, as `export -p`/`set`. */

static void print_quoted(const char *name, const char *value, const char *prefix)
{
  const char *p;

  printf("%s%s='", prefix, name);
  for (p = value; *p != '\0'; p++)
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

static bool is_reserved_word(const char *w)
{
  static const char *const words[] =
  {
    "!", "{", "}", "case", "do", "done", "elif", "else", "esac", "fi", "for",
    "if", "in", "then", "until", "while"
  };
  size_t i;

  for (i = 0; i < sizeof(words) / sizeof(words[0]); i++)
    {
      if (strcmp(w, words[i]) == 0)
        {
          return true;
        }
    }

  return false;
}

/* ---- Special builtins --------------------------------------------------------------- */

static int bi_colon(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  return 0;
}

static int bi_true(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  return 0;
}

static int bi_false(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  return 1;
}

static int bi_exit(int argc, char **argv)
{
  int status = g_sh.last_status;

  if (argc > 1 && !parse_count(argv[1], &status))
    {
      vs_err("exit: %s: numeric argument required", argv[1]);
      status = 2;
    }

  g_sh.last_status = status & 0xff;
  g_sh.unwind = UW_EXIT;
  return g_sh.last_status;
}

static int bi_return(int argc, char **argv)
{
  int status = g_sh.last_status;

  if (argc > 1 && !parse_count(argv[1], &status))
    {
      vs_err("return: %s: numeric argument required", argv[1]);
      status = 2;
    }

  if (g_sh.func_depth == 0 && g_sh.dot_depth == 0 &&
      vs_feat(VF_RETURN_TOPLEVEL_ERR))
    {
      vs_err("return: can only `return' from a function or sourced script");
      return 2;
    }

  g_sh.last_status = status & 0xff;
  g_sh.unwind = UW_RETURN;
  return g_sh.last_status;
}

static int loop_control(const char *name, enum unwind_e what, int argc,
                        char **argv)
{
  int n = 1;

  if (argc > 1 && (!parse_count(argv[1], &n) || n < 1))
    {
      vs_err("%s: %s: loop count out of range", name, argv[1]);
      return 1;
    }

  if (g_sh.loop_depth == 0)
    {
      return 0;
    }

  g_sh.unwind = what;
  g_sh.unwind_count = n > g_sh.loop_depth ? g_sh.loop_depth : n;
  return 0;
}

static int bi_break(int argc, char **argv)
{
  return loop_control("break", UW_BREAK, argc, argv);
}

static int bi_continue(int argc, char **argv)
{
  return loop_control("continue", UW_CONTINUE, argc, argv);
}

static int bi_eval(int argc, char **argv)
{
  struct sbuf_s text;
  int i;
  int status;

  if (argc < 2)
    {
      return 0;
    }

  sb_init(&text);
  for (i = 1; i < argc; i++)
    {
      if (i > 1)
        {
          sb_addc(&text, ' ');
        }

      sb_adds(&text, argv[i]);
    }

  g_sh.syntax_error = false;
  status = run_string(text.s != NULL ? text.s : "", text.len);
  sb_free(&text);
  if (g_sh.syntax_error && vs_feat(VF_EVAL_SYNTAX_FATAL) &&
      !g_sh.interactive && g_sh.unwind == UW_NONE)
    {
      g_sh.unwind = UW_EXIT;         /* a syntax error in eval is fatal */
      g_sh.last_status = 2;
    }

  return status;
}

/* Finds a file for `.`: as given if it has a '/', else via $PATH, else (in
 * profiles with VF_DOT_SEARCH_CWD, i.e. bash's default) the current
 * directory. NULL: not found.
 */

static char *find_sourced(const char *name)
{
  const char *path;
  struct sbuf_s cand;

  if (strchr(name, '/') != NULL || (path = var_get("PATH")) == NULL)
    {
      return vs_xstrdup(name);
    }

  sb_init(&cand);
  while (*path != '\0')
    {
      size_t len = strcspn(path, ":");
      struct stat st;

      cand.len = 0;
      if (len > 0)
        {
          sb_addn(&cand, path, len);
          sb_addc(&cand, '/');
        }

      sb_adds(&cand, name);
      if (stat(VS_FS(cand.s), &st) == 0 && S_ISREG(st.st_mode))
        {
          return sb_take(&cand);
        }

      path += len;
      if (*path == ':')
        {
          path++;
        }
    }

  sb_free(&cand);
  return vs_feat(VF_DOT_SEARCH_CWD) ? vs_xstrdup(name) : NULL;
}

static int bi_dot(int argc, char **argv)
{
  char *file;
  char **saved = NULL;
  int saved_n = 0;
  char **args = NULL;
  int status;
  int i;

  if (argc < 2)
    {
      vs_err("%s: filename argument required", argv[0]);
      return 2;
    }

  file = find_sourced(argv[1]);
  if (file == NULL)
    {
      vs_err("%s: file not found in PATH", argv[1]);
      vs_special_error();
      return 1;
    }

  if (argc > 2 && !vs_feat(VF_DOT_ARGS))
    {
      argc = 2;                   /* dash 0.5.12 ignores them */
    }

  if (argc > 2)
    {
      args = vs_xmalloc((size_t)(argc - 2) * sizeof(char *));
      for (i = 2; i < argc; i++)
        {
          args[i - 2] = vs_xstrdup(argv[i]);
        }

      saved = pos_swap(args, argc - 2, &saved_n);
    }

  g_sh.syntax_error = false;
  g_sh.dot_depth++;
  {
    struct frame_s frame;
    const char *outer_src = g_sh.cur_src;

    frame_push(&frame, "source", file, g_sh.lineno, false);
    g_sh.cur_src = file;
    status = run_file(file);
    g_sh.cur_src = outer_src;
    frame_pop(&frame);
  }

  g_sh.dot_depth--;
  if (g_sh.trap_action[VS_TRAP_RETURN] != NULL && g_sh.unwind == UW_NONE)
    {
      g_sh.last_status = status;
      trap_run_return();              /* a sourced script finished */
    }
  if (g_sh.syntax_error && vs_feat(VF_EVAL_SYNTAX_FATAL) &&
      !g_sh.interactive && g_sh.unwind == UW_NONE)
    {
      g_sh.unwind = UW_EXIT;
      g_sh.last_status = 2;
    }

  if (g_sh.unwind == UW_RETURN)
    {
      g_sh.unwind = UW_NONE;
      status = g_sh.last_status;
    }

  if (argc > 2)
    {
      for (i = 0; i < g_sh.npos; i++)
        {
          free(g_sh.pos[i]);
        }

      free(g_sh.pos);
      g_sh.pos = saved;
      g_sh.npos = saved_n;
    }

  free(file);
  return status;
}

static int bi_shift(int argc, char **argv)
{
  int n = 1;
  int i;

  if (argc > 1 && !parse_count(argv[1], &n))
    {
      vs_err("shift: %s: numeric argument required", argv[1]);
      return 2;
    }

  if (n > g_sh.npos)
    {
      vs_err("shift: shift count out of range");
      vs_special_error();
      return 1;
    }

  for (i = 0; i < n; i++)
    {
      free(g_sh.pos[i]);
    }

  memmove(g_sh.pos, g_sh.pos + n, (size_t)(g_sh.npos - n) * sizeof(char *));
  g_sh.npos -= n;
  return 0;
}

static int cmp_var_name(const void *a, const void *b)
{
  return strcmp((*(struct var_s *const *)a)->name,
                (*(struct var_s *const *)b)->name);
}

/* export / readonly: NAME[=VALUE]..., or -p / no arguments to list. */

static int mark_vars(int argc, char **argv, unsigned flag, const char *word)
{
  int i;
  int status = 0;
  bool list = (argc == 1);

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "-p") == 0)
        {
          list = true;
          continue;
        }

      if (strcmp(argv[i], "--") == 0)
        {
          continue;
        }

      {
        const char *eq = strchr(argv[i], '=');
        size_t nl = eq != NULL ? (size_t)(eq - argv[i]) : strlen(argv[i]);
        char *name = vs_xstrndup(argv[i], nl);

        if (!is_valid_name(name, nl))
          {
            vs_err("%s: `%s': not a valid identifier", word, argv[i]);
            status = 1;
          }
        else
          {
            if (eq != NULL && var_set(name, eq + 1) != 0)
              {
                status = 1;
                vs_special_error();
              }

            var_set_flags(name, flag);
          }

        free(name);
      }
    }

  if (list && vs_feat(VF_BASH_INFO_FORMATS))
    {
      /* bash: sorted `declare -x NAME="value"` lines. */

      struct var_s **vec;
      struct var_s *v;
      size_t n = 0;
      size_t k;

      for (v = g_sh.vars; v != NULL; v = v->next)
        {
          n += (v->flags & flag) != 0;
        }

      vec = vs_xmalloc((n + 1) * sizeof(*vec));
      n = 0;
      for (v = g_sh.vars; v != NULL; v = v->next)
        {
          if ((v->flags & flag) != 0)
            {
              vec[n++] = v;
            }
        }

      qsort(vec, n, sizeof(*vec), cmp_var_name);
      for (k = 0; k < n; k++)
        {
          const char *p;

          v = vec[k];
          printf("declare -%s%s %s", (v->flags & VF_READONLY) != 0 ? "r" : "",
                 (v->flags & VF_EXPORT) != 0 ? "x" : "", v->name);
          if ((v->flags & (VF_READONLY | VF_EXPORT)) == 0)
            {
              fputs("", stdout);
            }

          if (v->value != NULL)
            {
              fputs("=\"", stdout);
              for (p = v->value; *p != '\0'; p++)
                {
                  if (*p == '"' || *p == '\\' || *p == '$' || *p == '`')
                    {
                      putchar('\\');
                    }

                  putchar(*p);
                }

              putchar('"');
            }

          putchar('\n');
        }

      free(vec);
      return status;
    }

  if (list)
    {
      struct var_s *v;
      char prefix[32];

      snprintf(prefix, sizeof(prefix), "%s ", word);
      for (v = g_sh.vars; v != NULL; v = v->next)
        {
          if ((v->flags & flag) != 0)
            {
              if (v->value != NULL)
                {
                  print_quoted(v->name, v->value, prefix);
                }
              else
                {
                  printf("%s%s\n", prefix, v->name);
                }
            }
        }
    }

  return status;
}

static int bi_export(int argc, char **argv)
{
  if (vs_feat(VF_BASH_SYNTAX))
    {
      return bi_export_decl(argc, argv);
    }

  return mark_vars(argc, argv, VF_EXPORT, "export");
}

static int bi_readonly(int argc, char **argv)
{
  if (vs_feat(VF_BASH_SYNTAX))
    {
      return bi_readonly_decl(argc, argv);
    }

  return mark_vars(argc, argv, VF_READONLY, "readonly");
}

static int bi_unset(int argc, char **argv)
{
  bool funcs = false;
  bool refs = false;
  int status = 0;
  int i = 1;

  for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++)
    {
      if (strcmp(argv[i], "-f") == 0)
        {
          funcs = true;
        }
      else if (strcmp(argv[i], "-v") == 0)
        {
          funcs = false;
          refs = false;
        }
      else if (strcmp(argv[i], "-n") == 0 && vs_feat(VF_BASH_SYNTAX))
        {
          funcs = false;
          refs = true;                   /* the nameref itself, not what it names */
        }
      else if (strcmp(argv[i], "--") == 0)
        {
          i++;
          break;
        }
      else
        {
          vs_err("unset: %s: invalid option", argv[i]);
          vs_special_error();            /* unset is a special builtin: dash ends the shell */
          return 2;
        }
    }

  for (; i < argc; i++)
    {
      if (funcs)
        {
          func_unset(argv[i]);
        }
      else if (refs)
        {
          if (var_unset_raw(argv[i]) != 0)
            {
              status = 1;
            }
        }
      else
        {
          bool handled;
          int st = asg_unset_ref(argv[i], &handled);

          if (handled)
            {
              status = st != 0 ? 1 : status;
            }
          else if (var_unset(argv[i]) != 0)
            {
              status = 1;
            }
        }
    }

  return status;
}

static int set_option(char flag, bool on)
{
  switch (flag)
    {
      case 'e': g_sh.opt_e = on; return 0;
      case 'u': g_sh.opt_u = on; return 0;
      case 'x': g_sh.opt_x = on; return 0;
      case 'f': g_sh.opt_f = on; return 0;
      case 'C': g_sh.opt_C = on; return 0;
      case 'a': g_sh.opt_a = on; return 0;
      case 'n': g_sh.opt_n = on; return 0;
      case 'v': g_sh.opt_v = on; return 0;

      /* Accepted, not implemented: monitor mode (no job control yet),
       * asynchronous notification, ignoreeof.
       */

      case 'm':
      case 'b':
      case 'I': return 0;
      default:  return -1;
    }
}

/* The named (-o) options. 'flag' is the single-letter form; pipefail has
 * none. bash_only ones exist only where VF_SET_O_BASH does.
 */

static const struct
{
  const char *name;
  char flag;
  bool bash_only;
} g_named_opts[] =
{
  { "allexport", 'a', false }, { "errexit", 'e', false },
  { "noexec", 'n', false },    { "noglob", 'f', false },
  { "nounset", 'u', false },   { "verbose", 'v', false },
  { "xtrace", 'x', false },    { "noclobber", 'C', false },
  { "pipefail", 0, true }
};

#define NNAMED ((int)(sizeof(g_named_opts) / sizeof(g_named_opts[0])))

static bool named_opt_state(int i)
{
  switch (g_named_opts[i].flag)
    {
      case 'a': return g_sh.opt_a;
      case 'e': return g_sh.opt_e;
      case 'n': return g_sh.opt_n;
      case 'f': return g_sh.opt_f;
      case 'u': return g_sh.opt_u;
      case 'v': return g_sh.opt_v;
      case 'x': return g_sh.opt_x;
      case 'C': return g_sh.opt_C;
      default:  return g_sh.opt_pipefail;
    }
}

int vs_set_named_option(const char *name, bool on)
{
  int i;

  if (strcmp(name, "posix") == 0 && vs_feat(VF_SET_O_BASH))
    {
      vs_mode_set(on ? VS_PROFILE_POSIX : VS_PROFILE_BASH);
      return 0;
    }

  for (i = 0; i < NNAMED; i++)
    {
      if (strcmp(name, g_named_opts[i].name) != 0)
        {
          continue;
        }

      if (g_named_opts[i].bash_only && !vs_feat(VF_SET_O_BASH))
        {
          return -1;
        }

      if (g_named_opts[i].flag == 0)
        {
          g_sh.opt_pipefail = on;
          return 0;
        }

      return set_option(g_named_opts[i].flag, on);
    }

  return -1;
}

/* 1 if the named option is on, 0 if off, -1 if there is no such option. */

int vs_option_state(const char *name)
{
  int i;

  for (i = 0; i < NNAMED; i++)
    {
      if (strcmp(name, g_named_opts[i].name) == 0 &&
          (!g_named_opts[i].bash_only || vs_feat(VF_SET_O_BASH)))
        {
          return named_opt_state(i) ? 1 : 0;
        }
    }

  return -1;
}

/* `set -o` (human readable) and `set +o` (re-readable). */

static void list_options(bool reusable)
{
  bool bash = vs_feat(VF_BASH_INFO_FORMATS);
  int i;

  if (!reusable && !bash)
    {
      puts("Current option settings");
    }

  for (i = 0; i < NNAMED; i++)
    {
      bool on;

      if (g_named_opts[i].bash_only && !vs_feat(VF_SET_O_BASH))
        {
          continue;
        }

      on = named_opt_state(i);
      if (reusable)
        {
          printf("set %co %s\n", on ? '-' : '+', g_named_opts[i].name);
        }
      else if (bash)
        {
          printf("%-15s\t%s\n", g_named_opts[i].name, on ? "on" : "off");
        }
      else
        {
          printf("%-15s %s\n", g_named_opts[i].name, on ? "on" : "off");
        }
    }
}

/* An array as `set` prints it: a=([0]="x" [1]="y z"). Inside the double
 * quotes " \ $ and ` are escaped.
 */

/* `set` prints a scalar bare unless bash would have to quote it: a leading
 * ~ or #, a shell metacharacter, or a control character.
 */

static void print_set_scalar(const char *name, const char *value)
{
  const char *p;
  bool quote = value[0] == '~' || value[0] == '#';

  for (p = value; *p != '\0' && !quote; p++)
    {
      quote = strchr(" \t\n'\"\\|&;()<>!{}*[?]^$`", *p) != NULL ||
              (unsigned char)*p < 0x20 || *p == 0x7f;
    }

  if (quote)
    {
      char *q = vs_quote_word(value);

      printf("%s=%s\n", name, q);
      free(q);
    }
  else
    {
      printf("%s=%s\n", name, value);
    }
}

static int cmp_var_names(const void *a, const void *b)
{
  return strcmp((*(struct var_s *const *)a)->name,     /* bash sorts by bytes here, not by locale */
                (*(struct var_s *const *)b)->name);
}

/* The text of a value as `set` and `declare -p` show it: in double quotes, or
 * as $'...' when it holds control characters, which is what bash does.
 */

void vs_print_dq(const char *s)
{
  const char *p;

  for (p = s; *p != '\0'; p++)
    {
      if ((unsigned char)*p < 0x20 || *p == 0x7f)
        {
          char *q = vs_quote_word(s);

          fputs(q, stdout);
          free(q);
          return;
        }
    }

  putchar('"');
  for (p = s; *p != '\0'; p++)
    {
      if (*p == '"' || *p == '\\' || *p == '$' || *p == '`')
        {
          putchar('\\');
        }

      putchar(*p);
    }

  putchar('"');
}

/* The (...) part: ([0]="x" [1]="y z") for an indexed array, and
 * ([k]="v" ) for an associative one -- bash leaves a space before the ).
 */

void vs_print_array_body(const struct arr_s *a)
{
  size_t i;

  putchar('(');
  for (i = 0; i < a->n; i++)
    {
      if (a->assoc)
        {
          bool plain = a->e[i].key[0] != '\0';
          const char *k;

          for (k = a->e[i].key; *k != '\0' && plain; k++)
            {
              plain = (*k >= 'a' && *k <= 'z') || (*k >= 'A' && *k <= 'Z') ||
                      (*k >= '0' && *k <= '9') || *k == '_';
            }

          if (plain)
            {
              printf("[%s]=", a->e[i].key);
            }
          else
            {
              putchar('[');
              vs_print_dq(a->e[i].key);
              fputs("]=", stdout);
            }

          vs_print_dq(a->e[i].val);
          putchar(' ');
        }
      else
        {
          printf("%s[%ld]=", i > 0 ? " " : "", a->e[i].idx);
          vs_print_dq(a->e[i].val);
        }
    }

  putchar(')');
}

static void print_array(const struct var_s *v)
{
  printf("%s=", v->name);
  vs_print_array_body(v->arr);
  putchar('\n');
}

static int bi_set(int argc, char **argv)
{
  int i = 1;
  bool setpos = false;

  if (argc == 1)
    {
      struct var_s *v;
      struct var_s **sorted;
      size_t nv = 0;
      size_t k;

      /* bash lists them by name */

      for (v = g_sh.vars; v != NULL; v = v->next)
        {
          nv++;
        }

      sorted = vs_xmalloc((nv + 1) * sizeof(*sorted));
      for (nv = 0, v = g_sh.vars; v != NULL; v = v->next)
        {
          sorted[nv++] = v;
        }

      qsort(sorted, nv, sizeof(*sorted), cmp_var_names);
      for (k = 0; k < nv; k++)
        {
          v = sorted[k];
          if (v->arr != NULL)
            {
              print_array(v);
            }
          else if (v->value != NULL)
            {
              if (vs_feat(VF_BASH_SYNTAX))
                {
                  print_set_scalar(v->name, v->value);      /* bash: quoted only when needed */
                }
              else
                {
                  print_quoted(v->name, v->value, "");      /* dash: always */
                }
            }
        }

      free(sorted);
      return 0;
    }

  for (; i < argc; i++)
    {
      const char *a = argv[i];
      bool on;

      if (strcmp(a, "--") == 0)
        {
          i++;
          setpos = true;
          break;
        }

      if ((a[0] != '-' && a[0] != '+') || a[1] == '\0')
        {
          setpos = true;
          break;
        }

      on = (a[0] == '-');
      if (strcmp(a + 1, "o") == 0 && (i + 1 >= argc || argv[i + 1][0] == '-' ||
                                      argv[i + 1][0] == '+'))
        {
          list_options(!on);
          continue;
        }

      if (strcmp(a + 1, "o") == 0)
        {
          if (i + 1 >= argc || vs_set_named_option(argv[i + 1], on) != 0)
            {
              vs_err("set: %s: invalid option name",
                     i + 1 < argc ? argv[i + 1] : "(missing)");
              vs_special_error();
              return 2;
            }

          i++;
          continue;
        }

      {
        const char *f;

        for (f = a + 1; *f != '\0'; f++)
          {
            if (set_option(*f, on) != 0)
              {
                vs_err("set: %c%c: invalid option", a[0], *f);
                vs_special_error();
                return 2;
              }
          }
      }
    }

  if (setpos)
    {
      pos_set(argv + i, argc - i);
    }

  return 0;
}

static int bi_exec(int argc, char **argv)
{
  char *path;
  char **envp;
  int err = 0;

  if (argc < 2)
    {
      return 0;                 /* redirections were made permanent */
    }

  if (!vs_plat_have_fork() || g_sh.in_subshell > 0)
    {
      /* No process to replace (NuttX), or an in-process subshell whose
       * "process" is the shell itself: run the command, then finish.
       */

      int st = run_argv(argc - 1, argv + 1, true);

      g_sh.unwind = UW_EXIT;
      g_sh.last_status = st;
      return st;
    }

  path = vs_plat_find_command(argv[1], var_get("PATH"), &err);
  if (path == NULL)
    {
      vs_err("exec: %s: %s", argv[1],
             err == EACCES ? "Permission denied" : "not found");
      g_sh.unwind = g_sh.interactive ? UW_NONE : UW_EXIT;
      g_sh.last_status = err == EACCES ? 126 : 127;
      return g_sh.last_status;
    }

  envp = var_build_env();
  fflush(NULL);
  vs_plat_exec(path, argv + 1, envp);
  vs_err("exec: %s: %s", argv[1], strerror(errno));
  return 126;
}

/* ---- Regular builtins ------------------------------------------------------------------ */

/* True if 'a' and 'b' name the same directory. */

static bool same_dir(const char *a, const char *b)
{
  struct stat sa;
  struct stat sb;

  return stat(VS_FS(a), &sa) == 0 && stat(VS_FS(b), &sb) == 0 &&
         sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

/* The logical working directory: $PWD if it still names the cwd, else
 * whatever the OS says. Returns false if neither is available.
 */

static bool logical_cwd(char *out, size_t n)
{
  const char *pwd = var_get("PWD");

  if (pwd != NULL && pwd[0] == '/' && strlen(pwd) < n && same_dir(pwd, "."))
    {
      strcpy(out, pwd);
      return true;
    }

  return getcwd(out, n) != NULL;
}

/* cd [-L|-P] [dir | -]. -L (the default) works on the logical path: "."
 * and ".." are resolved lexically against $PWD, as POSIX specifies, and
 * $PWD keeps the logical result; -P resolves symlinks and sets $PWD from
 * getcwd(). CDPATH is honoured for relative names.
 */

static int bi_cd(int argc, char **argv)
{
  bool physical = false;
  bool print = false;
  const char *dir;
  const char *target;
  char oldpwd[VS_PATH_MAX];
  char curpath[VS_PATH_MAX];
  char cdcand[VS_PATH_MAX];
  int i = 1;

  for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++)
    {
      if (strcmp(argv[i], "-L") == 0)
        {
          physical = false;
        }
      else if (strcmp(argv[i], "-P") == 0)
        {
          physical = true;
        }
      else if (strcmp(argv[i], "--") == 0)
        {
          i++;
          break;
        }
      else
        {
          vs_err("cd: %s: invalid option", argv[i]);
          return 2;
        }
    }

  if (i >= argc)
    {
      dir = var_get("HOME");
      if (dir == NULL || dir[0] == '\0')
        {
          vs_err("cd: HOME not set");
          return 1;
        }
    }
  else if (strcmp(argv[i], "-") == 0)
    {
      dir = var_get("OLDPWD");
      if (dir == NULL)
        {
          vs_err("cd: OLDPWD not set");
          return 1;
        }

      print = true;
    }
  else
    {
      dir = argv[i];
    }

  if (dir[0] == '\0')
    {
      vs_err("cd: null directory");
      return 1;
    }

  target = dir;

  /* CDPATH applies to names that are not absolute or explicitly relative. */

  if (dir[0] != '/' && strcmp(dir, ".") != 0 && strcmp(dir, "..") != 0 &&
      strncmp(dir, "./", 2) != 0 && strncmp(dir, "../", 3) != 0 &&
      var_get("CDPATH") != NULL)
    {
      const char *cp = var_get("CDPATH");

      for (; ; )
        {
          size_t len = strcspn(cp, ":");
          struct stat st;

          if (len == 0)
            {
              snprintf(cdcand, sizeof(cdcand), "./%s", dir);
            }
          else
            {
              snprintf(cdcand, sizeof(cdcand), "%.*s/%s", (int)len, cp, dir);
            }

          if (stat(VS_FS(cdcand), &st) == 0 && S_ISDIR(st.st_mode))
            {
              target = cdcand;
              print = print || len > 0;
              break;
            }

          if (cp[len] == '\0')
            {
              break;
            }

          cp += len + 1;
        }
    }

  if (!logical_cwd(oldpwd, sizeof(oldpwd)))
    {
      oldpwd[0] = '\0';
    }

  if (physical || oldpwd[0] == '\0')
    {
      if (chdir(VS_FS(target)) != 0)
        {
          vs_err("cd: %s: %s", dir, strerror(errno));
          return vs_feat(VF_EXIT2_ON_ERROR) ? 2 : 1;
        }

      if (getcwd(curpath, sizeof(curpath)) == NULL)
        {
          curpath[0] = '\0';
        }
    }
  else
    {
      if (vs_path_normalize(oldpwd, target, curpath, sizeof(curpath)) != 0 ||
          chdir(VS_FS(curpath)) != 0)
        {
          vs_err("cd: %s: %s", dir, strerror(errno != 0 ? errno : ENAMETOOLONG));
          return vs_feat(VF_EXIT2_ON_ERROR) ? 2 : 1;
        }
    }

  if (oldpwd[0] != '\0')
    {
      var_set("OLDPWD", oldpwd);
    }

  if (curpath[0] != '\0')
    {
      var_set("PWD", curpath);
      if (print)
        {
          puts(curpath);
        }
    }

  return 0;
}

static int bi_pwd(int argc, char **argv)
{
  bool physical = false;
  char cwd[VS_PATH_MAX];
  int i;

  for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++)
    {
      if (strcmp(argv[i], "-P") == 0)
        {
          physical = true;
        }
      else if (strcmp(argv[i], "-L") == 0)
        {
          physical = false;
        }
      else if (strcmp(argv[i], "--") == 0)
        {
          break;
        }
      else
        {
          vs_err("pwd: %s: invalid option", argv[i]);
          return 2;
        }
    }

  if (!physical && logical_cwd(cwd, sizeof(cwd)))
    {
      puts(cwd);
      return 0;
    }

  if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
      vs_err("pwd: %s", strerror(errno));
      return 1;
    }

  puts(cwd);
  return 0;
}

/* Waits up to ms for fd to be readable: 1 ready, 0 timed out. */

static int wait_readable(int fd, long ms)
{
  struct pollfd pfd;

  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  return poll(&pfd, 1, (int)ms) > 0 ? 1 : 0;
}

/* read [-r] [-a array] [-d delim] [-n nchars] [-p prompt] [-t timeout] [-u fd]
 *      [name ...]
 *
 * Options may be bundled (`read -ra parts`); an option that takes a value
 * takes the rest of its bundle, or else the next word. The line is cut into
 * fields by read_split() (readsplit.c).
 */

static int bi_read(int argc, char **argv)
{
  int rfd = STDIN_FILENO;
  int delim = '\n';
  long maxn = -1;
  long timeout_ms = -1;
  const char *prompt = NULL;
  const char *array = NULL;
  bool raw = false;
  int i = 1;
  struct sbuf_s line;
  const char *ifs = var_get("IFS") != NULL ? var_get("IFS") : " \t\n";
  bool eof = false;
  int nvars;
  char **names;
  struct fieldv_s fields;
  int badname = vs_feat(VF_EXIT2_ON_ERROR) ? 2 : 1;
  bool whole = false;
  size_t cstart = 0;                    /* where the character being read began */
  long nchars = 0;
  int k;

  for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++)
    {
      const char *c;

      if (strcmp(argv[i], "--") == 0)
        {
          i++;
          break;
        }

      for (c = argv[i] + 1; *c != '\0'; c++)
        {
          char opt = *c;
          const char *val;

          if (opt == 'r')
            {
              raw = true;
              continue;
            }

          if (opt == 's' && vs_feat(VF_READ_EXT))
            {
              continue;                /* no terminal echo control yet */
            }

          if (opt != 'p' && !(vs_feat(VF_READ_EXT) && strchr("andtu", opt) != NULL))
            {
              vs_err("read: -%c: invalid option", opt);
              return 2;
            }

          if (c[1] != '\0')
            {
              val = c + 1;                        /* -nVALUE, -pPROMPT */
              c += strlen(c) - 1;
            }
          else if (i + 1 < argc)
            {
              val = argv[++i];
            }
          else
            {
              vs_err("read: -%c: option requires an argument", opt);
              return 2;
            }

          switch (opt)
            {
              case 'p': prompt = val; break;
              case 'a': array = val; break;
              case 'n': maxn = atol(val); break;
              case 'd': delim = val[0] != '\0' ? (unsigned char)val[0] : 0; break;
              case 't': timeout_ms = (long)(atof(val) * 1000.0); break;
              default:  rfd = atoi(val); break;
            }
        }
    }

  nvars = argc - i;
  names = argv + i;
  if (array != NULL)
    {
      /* read -a: the fields go to one array; any names after it are ignored */

      if (!is_valid_name(array, strlen(array)))
        {
          vs_err("read: `%s': not a valid identifier", array);
          return badname;
        }

      if (var_is_assoc(array))
        {
          vs_err("read: %s: not an indexed array", array);
          return 1;
        }

      nvars = 0;
    }
  else if (nvars == 0)
    {
      static char *reply[] = { "REPLY" };

      names = reply;
      nvars = 1;
      whole = true;                /* bash: REPLY is the line as read, unsplit */
    }

  for (k = 0; array == NULL && k < nvars; k++)
    {
      if (!is_valid_name(names[k], strlen(names[k])))
        {
          vs_err("read: `%s': not a valid identifier", names[k]);
          return badname;
        }
    }

  /* bash shows -p only when reading from a terminal. */

  if (prompt != NULL && vs_plat_isatty(rfd))
    {
      fputs(prompt, stderr);
      fflush(stderr);
    }

  if (timeout_ms >= 0)
    {
      if (!wait_readable(rfd, timeout_ms))
        {
          return timeout_ms == 0 ? 1 : 142;
        }

      if (timeout_ms == 0)
        {
          return 0;               /* input is available */
        }
    }

  /* One byte at a time: a shell must not consume input past the newline. */

  sb_init(&line);
  for (; ; )
    {
      char c;
      ssize_t n = read(rfd, &c, 1);

      if (n <= 0)
        {
          eof = true;
          break;
        }

      if (c == delim)
        {
          break;
        }

      if (c == '\\' && !raw)
        {
          n = read(rfd, &c, 1);
          if (n <= 0)
            {
              eof = true;
              break;
            }

          if (c == '\n')
            {
              continue;
            }

          sb_addc(&line, '\\');       /* kept, marks the next char literal */
        }

      sb_addc(&line, c);

      /* -n counts characters: a multibyte one is complete only when its last
       * byte has come
       */

      while (cstart < line.len)
        {
          long wc;
          size_t r = vs_plat_multibyte() ? vs_plat_mbdecode(line.s + cstart, line.len - cstart, &wc) : 1;

          if (r == 0)
            {
              break;                       /* the character is not all here yet */
            }

          cstart += r == (size_t)-1 ? 1 : r;
          nchars++;
        }

      if (maxn > 0 && nchars >= maxn)
        {
          break;
        }
    }

  fv_init(&fields);
  read_split(line.s != NULL ? line.s : "", ifs, raw, whole ? -1 : nvars, &fields);
  if (array != NULL)
    {
      /* a fresh array, then one element per field (so -i/-l/-u apply) */

      if (var_array_replace(array, arr_new()) != 0)
        {
          fv_free(&fields);
          sb_free(&line);
          return 1;
        }

      for (k = 0; k < fields.n; k++)
        {
          var_elem_set(array, k, fields.v[k]);
        }
    }
  else
    {
      for (k = 0; k < nvars; k++)
        {
          var_set(names[k], fields.v[k]);
        }
    }

  fv_free(&fields);
  sb_free(&line);
  return eof ? 1 : 0;
}

/* let expr...: each argument is an arithmetic expression; status is 0 if
 * the last one is non-zero.
 */

static int bi_let(int argc, char **argv)
{
  long v = 0;
  int i;

  if (argc < 2)
    {
      vs_err("let: expression expected");
      return 1;
    }

  for (i = 1; i < argc; i++)
    {
      if (arith_eval(argv[i], &v) != 0)
        {
          return 1;
        }
    }

  return v != 0 ? 0 : 1;
}

/* builtin cmd args: run a builtin, bypassing functions. */

static int bi_builtin(int argc, char **argv)
{
  if (argc < 2)
    {
      return 0;
    }

  if (builtin_find(argv[1]) == NULL)
    {
      vs_err("builtin: %s: not a shell builtin", argv[1]);
      return 1;
    }

  return run_argv(argc - 1, argv + 1, true);
}

static int bi_command(int argc, char **argv)
{
  int i = 1;
  bool verbose = false;

  for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++)
    {
      if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-V") == 0)
        {
          verbose = true;
        }
      else if (strcmp(argv[i], "-p") == 0)
        {
          continue;
        }
      else if (strcmp(argv[i], "--") == 0)
        {
          i++;
          break;
        }
      else
        {
          vs_err("command: %s: invalid option", argv[i]);
          return 2;
        }
    }

  if (i >= argc)
    {
      return 0;
    }

  if (verbose)
    {
      int status = 0;

      for (; i < argc; i++)
        {
          char *path;

          if (is_reserved_word(argv[i]))
            {
              puts(argv[i]);
              continue;
            }

          switch (classify_command(argv[i], &path))
            {
              case CK_SPECIAL:
              case CK_BUILTIN:
              case CK_FUNCTION:
                puts(argv[i]);
                break;
              case CK_EXTERNAL:
                puts(path);
                free(path);
                break;
              default:
                status = vs_feat(VF_NOTFOUND_127) ? 127 : 1;
                break;
            }
        }

      return status;
    }

  return run_argv(argc - i, argv + i, true);
}

static int bi_type(int argc, char **argv)
{
  int status = 0;
  bool terse = false;
  int i = 1;

  if (i < argc && strcmp(argv[i], "-t") == 0 && vs_feat(VF_BASH_INFO_FORMATS))
    {
      terse = true;
      i++;
    }

  for (; i < argc; i++)
    {
      char *path;

      if (is_reserved_word(argv[i]))
        {
          if (terse)
            {
              puts("keyword");
            }
          else
            {
              printf("%s is a shell keyword\n", argv[i]);
            }

          continue;
        }

      if (terse)
        {
          switch (classify_command(argv[i], &path))
            {
              case CK_SPECIAL:
              case CK_BUILTIN:
                puts("builtin");
                break;
              case CK_FUNCTION:
                puts("function");
                break;
              case CK_EXTERNAL:
                puts("file");
                free(path);
                break;
              default:
                status = 1;
                break;
            }

          continue;
        }

      switch (classify_command(argv[i], &path))
        {
          case CK_SPECIAL:
            printf("%s is a special shell builtin\n", argv[i]);
            break;
          case CK_BUILTIN:
            printf("%s is a shell builtin\n", argv[i]);
            break;
          case CK_FUNCTION:
            printf("%s is a function\n", argv[i]);
            break;
          case CK_EXTERNAL:
            printf("%s is %s\n", argv[i], path);
            free(path);
            break;
          default:
            if (vs_feat(VF_NOTFOUND_127))
              {
                printf("%s: not found\n", argv[i]);   /* dash: on stdout */
                status = 127;
              }
            else
              {
                vs_err("type: %s: not found", argv[i]);
                status = 1;
              }

            break;
        }
    }

  return status;
}

static int bi_wait(int argc, char **argv)
{
  int status = 0;
  int i;

  if (argc == 1)
    {
      int ws;

      while (waitpid(-1, &ws, 0) > 0 || errno == EINTR)
        {
        }

      return 0;
    }

  for (i = 1; i < argc; i++)
    {
      int ws;
      pid_t pid = (pid_t)atol(argv[i]);

      if (waitpid(pid, &ws, 0) < 0)
        {
          vs_err("wait: pid %s is not a child of this shell", argv[i]);
          status = 127;
        }
      else
        {
          status = WIFEXITED(ws) ? WEXITSTATUS(ws) : 128 + WTERMSIG(ws);
        }
    }

  return status;
}

/* Applies a symbolic mode (u=rwx,go=rx / +w / -x) to 'perm' (the
 * permissions umask leaves, not the mask itself). Returns false if the
 * text is not one.
 */

static bool apply_symbolic(const char *spec, unsigned *perm)
{
  const char *p = spec;

  for (; ; )
    {
      unsigned who = 0;
      unsigned bits;

      while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a')
        {
          who |= (*p == 'u') ? 0700 : (*p == 'g') ? 0070 : (*p == 'o') ? 0007 : 0777;
          p++;
        }

      if (who == 0)
        {
          who = 0777;
        }

      if (*p != '=' && *p != '+' && *p != '-')
        {
          return false;
        }

      {
        char op = *p++;

        bits = 0;
        for (; *p != '\0' && *p != ','; p++)
          {
            if (*p == 'r') bits |= 0444;
            else if (*p == 'w') bits |= 0222;
            else if (*p == 'x') bits |= 0111;
            else return false;
          }

        bits &= who;
        if (op == '=')
          {
            *perm = (*perm & ~who) | bits;
          }
        else if (op == '+')
          {
            *perm |= bits;
          }
        else
          {
            *perm &= ~bits;
          }
      }

      if (*p == '\0')
        {
          return true;
        }

      p++;                              /* the comma */
    }
}

static int bi_umask(int argc, char **argv)
{
  bool symbolic = false;
  mode_t m;
  int i = 1;

  if (i < argc && strcmp(argv[i], "-S") == 0)
    {
      symbolic = true;
      i++;
    }

  if (i < argc)
    {
      char *end;
      long v = strtol(argv[i], &end, 8);

      if (*argv[i] != '\0' && *end == '\0' && v >= 0 && v <= 0777)
        {
          umask((mode_t)v);
          return 0;
        }

      m = umask(0);
      umask(m);
      {
        unsigned perm = (~(unsigned)m) & 0777;

        if (!apply_symbolic(argv[i], &perm))
          {
            umask(m);
            vs_err("umask: %s: invalid mode", argv[i]);
            return vs_feat(VF_EXIT2_ON_ERROR) ? 2 : 1;
          }

        umask((mode_t)(~perm & 0777));
      }

      return 0;
    }

  m = umask(0);
  umask(m);
  if (symbolic)
    {
      unsigned perm = (~(unsigned)m) & 0777;
      char out[40];
      char *o = out;
      int k;

      for (k = 0; k < 3; k++)
        {
          unsigned bits = (perm >> (6 - 3 * k)) & 7;

          *o++ = "ugo"[k];
          *o++ = '=';
          if (bits & 4) *o++ = 'r';
          if (bits & 2) *o++ = 'w';
          if (bits & 1) *o++ = 'x';
          if (k < 2) *o++ = ',';
        }

      *o = '\0';
      puts(out);
      return 0;
    }

  printf("%04o\n", (unsigned)m);
  return 0;
}

/* ---- The table --------------------------------------------------------------------------- */

const struct builtin_s g_vs_builtins[] =
{
  { ":",        bi_colon,    true,  "do nothing, successfully", VS_M_ALL },
  { ".",        bi_dot,      true,  ". file [args]: run commands from a file in this shell", VS_M_ALL },
  { "break",    bi_break,    true,  "break [n]: leave a loop", VS_M_ALL },
  { "continue", bi_continue, true,  "continue [n]: next loop iteration", VS_M_ALL },
  { "eval",     bi_eval,     true,  "eval [args]: run the arguments as a command", VS_M_ALL },
  { "exec",     bi_exec,     true,  "exec [command]: replace the shell / apply redirections", VS_M_ALL },
  { "exit",     bi_exit,     true,  "exit [n]: leave the shell", VS_M_ALL },
  { "export",   bi_export,   true,  "export [-p] [name[=value]]: mark variables for export", VS_M_ALL },
  { "readonly", bi_readonly, true,  "readonly [-p] [name[=value]]: make variables read-only", VS_M_ALL },
  { "return",   bi_return,   true,  "return [n]: leave a function or sourced file", VS_M_ALL },
  { "set",      bi_set,      true,  "set [-euxfC] [-o name] [--] [args]: options and positional parameters", VS_M_ALL },
  { "shift",    bi_shift,    true,  "shift [n]: drop positional parameters", VS_M_ALL },
  { "source",   bi_dot,      true,  "source file [args]: same as .", VS_M_BASH },
  { "trap",     bi_trap,     true,  "trap [action sig...]: run action on signals / exit", VS_M_ALL },
  { "unset",    bi_unset,    true,  "unset [-fv] name...: remove variables or functions", VS_M_ALL },
  { "[",        bi_bracket,  false, "[ expr ]: evaluate a conditional expression", VS_M_ALL },
  { "cd",       bi_cd,       false, "cd [dir | -]: change directory", VS_M_ALL },
  { "command",  bi_command,  false, "command [-v] name [args]: run bypassing functions", VS_M_ALL },
  { "alias",    bi_alias,    false, "alias [name[=value]...]: define or show aliases", VS_M_ALL },
  { "unalias",  bi_unalias,  false, "unalias [-a] name...: remove aliases", VS_M_ALL },
  { "getopts",  bi_getopts,  false, "getopts optstring name [arg...]: parse options", VS_M_ALL },
  { "hash",     bi_hash,     false, "hash [-r] [name...]: remember command locations", VS_M_ALL },
  { "declare",  bi_declare,  false, "declare [-aAilrux] [-p] [name[=value] ...]: set variable attributes", VS_M_BASH },
  { "mapfile",  bi_mapfile,  false, "mapfile [-d delim] [-n count] [-O origin] [-s count] [-t] [-u fd] [-C callback] [-c quantum] [array]: read lines into an array", VS_M_BASH },
  { "readarray", bi_mapfile, false, "readarray [-d delim] [-n count] [-O origin] [-s count] [-t] [-u fd] [-C callback] [-c quantum] [array]: a synonym for mapfile", VS_M_BASH },
  { "typeset",  bi_declare,  false, "typeset [-aAilrux] [-p] [name[=value] ...]: a synonym for declare", VS_M_BASH },
  { "local",    bi_local,    false, "local [name[=value]...]: function-local variables", VS_M_ALL },
#ifdef VAPORSHELL_POSIX
  { "times",    bi_times,    true,  "print accumulated process times", VS_M_ALL },
  { "ulimit",   bi_ulimit,   false, "ulimit [-HS] [-cdfnstv] [limit]: resource limits", VS_M_ALL },
#endif
  { "echo",     bi_echo,     false, "echo [-neE] [arg...]: print arguments", VS_M_ALL },
  { "false",    bi_false,    false, "do nothing, unsuccessfully", VS_M_ALL },
  { "help",     bi_help,     false, "help [name]: list builtins", VS_M_BASH },
  { "kill",     bi_kill,     false, "kill [-s sig | -sig] pid...: send a signal", VS_M_ALL },
  { "printf",   bi_printf,   false, "printf [-v var] format [arg...]: formatted output", VS_M_ALL },
  { "pwd",      bi_pwd,      false, "print the working directory", VS_M_ALL },
  { "read",     bi_read,     false, "read [-r] name...: read a line into variables", VS_M_ALL },
  { "test",     bi_test,     false, "test expr: evaluate a conditional expression", VS_M_ALL },
  { "true",     bi_true,     false, "do nothing, successfully", VS_M_ALL },
  { "type",     bi_type,     false, "type name...: say how a name resolves", VS_M_ALL },
  { "pushd",    bi_pushd,    false, "pushd [-n] [dir | +N | -N]: push a directory", VS_M_BASH },
  { "popd",     bi_popd,     false, "popd [-n] [+N | -N]: pop a directory", VS_M_BASH },
  { "dirs",     bi_dirs,     false, "dirs [-clpv]: show the directory stack", VS_M_BASH },
  { "shopt",    bi_shopt,    false, "shopt [-pqsu] [-o] [name...]: shell options", VS_M_BASH },
  { "let",      bi_let,      false, "let expr...: evaluate arithmetic expressions", VS_M_BASH },
  { "builtin",  bi_builtin,  false, "builtin cmd [args]: run a builtin, skipping functions", VS_M_BASH },
  { "umask",    bi_umask,    false, "umask [mode]: show or set the file creation mask", VS_M_ALL },
  { "wait",     bi_wait,     false, "wait [pid...]: wait for background jobs", VS_M_ALL },
  { NULL, NULL, false, NULL, 0 }
};

const struct builtin_s *builtin_find(const char *name)
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
