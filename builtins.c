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
      if (stat(cand.s, &st) == 0 && S_ISREG(st.st_mode))
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
  status = run_file(file);
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
  return mark_vars(argc, argv, VF_EXPORT, "export");
}

static int bi_readonly(int argc, char **argv)
{
  return mark_vars(argc, argv, VF_READONLY, "readonly");
}

static int bi_unset(int argc, char **argv)
{
  bool funcs = false;
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
        }
      else if (strcmp(argv[i], "--") == 0)
        {
          i++;
          break;
        }
      else
        {
          vs_err("unset: %s: invalid option", argv[i]);
          return 2;
        }
    }

  for (; i < argc; i++)
    {
      if (funcs)
        {
          func_unset(argv[i]);
        }
      else if (var_unset(argv[i]) != 0)
        {
          status = 1;
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
      default:  return -1;
    }
}

int vs_set_named_option(const char *name, bool on)
{
  if (strcmp(name, "posix") == 0)
    {
      vs_mode_set(on ? VS_PROFILE_POSIX : VS_PROFILE_BASH);
      return 0;
    }

  static const struct
  {
    const char *name;
    char flag;
  } names[] =
  {
    { "errexit", 'e' }, { "nounset", 'u' }, { "xtrace", 'x' },
    { "noglob", 'f' }, { "noclobber", 'C' }
  };
  size_t i;

  for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
      if (strcmp(name, names[i].name) == 0)
        {
          return set_option(names[i].flag, on);
        }
    }

  return -1;
}

static int bi_set(int argc, char **argv)
{
  int i = 1;
  bool setpos = false;

  if (argc == 1)
    {
      struct var_s *v;

      for (v = g_sh.vars; v != NULL; v = v->next)
        {
          if (v->value != NULL)
            {
              print_quoted(v->name, v->value, "");
            }
        }

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
      if (strcmp(a + 1, "o") == 0)
        {
          if (i + 1 >= argc || vs_set_named_option(argv[i + 1], on) != 0)
            {
              vs_err("set: %s: invalid option name",
                     i + 1 < argc ? argv[i + 1] : "(missing)");
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

  if (!vs_plat_have_fork())
    {
      vs_err("exec: not supported on this platform yet");
      return 1;
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

static int bi_cd(int argc, char **argv)
{
  const char *dir;
  char cwd[512];
  char *old;
  bool print = false;
  int i = 1;

  while (i < argc && (strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "-P") == 0))
    {
      i++;
    }

  if (i < argc && strcmp(argv[i], "--") == 0)
    {
      i++;
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

  old = getcwd(cwd, sizeof(cwd)) != NULL ? vs_xstrdup(cwd) : NULL;
  if (chdir(dir) != 0)
    {
      vs_err("cd: %s: %s", dir, strerror(errno));
      free(old);
      return 1;
    }

  if (old != NULL)
    {
      var_set("OLDPWD", old);
    }

  if (getcwd(cwd, sizeof(cwd)) != NULL)
    {
      var_set("PWD", cwd);
      if (print)
        {
          puts(cwd);
        }
    }

  free(old);
  return 0;
}

static int bi_pwd(int argc, char **argv)
{
  char cwd[512];

  (void)argc;
  (void)argv;
  if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
      vs_err("pwd: %s", strerror(errno));
      return 1;
    }

  puts(cwd);
  return 0;
}

static int bi_read(int argc, char **argv)
{
  bool raw = false;
  int i = 1;
  struct sbuf_s line;
  const char *ifs = var_get("IFS") != NULL ? var_get("IFS") : " \t\n";
  bool eof = false;
  int nvars;
  char **names;
  const char *p;
  int k;

  for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++)
    {
      if (strcmp(argv[i], "-r") == 0)
        {
          raw = true;
        }
      else if (strcmp(argv[i], "--") == 0)
        {
          i++;
          break;
        }
      else
        {
          vs_err("read: %s: invalid option", argv[i]);
          return 2;
        }
    }

  nvars = argc - i;
  names = argv + i;
  if (nvars == 0)
    {
      static char *reply[] = { "REPLY" };

      names = reply;
      nvars = 1;
    }

  for (k = 0; k < nvars; k++)
    {
      if (!is_valid_name(names[k], strlen(names[k])))
        {
          vs_err("read: `%s': not a valid identifier", names[k]);
          return 2;
        }
    }

  /* One byte at a time: a shell must not consume input past the newline. */

  sb_init(&line);
  for (; ; )
    {
      char c;
      ssize_t n = read(STDIN_FILENO, &c, 1);

      if (n <= 0)
        {
          eof = true;
          break;
        }

      if (c == '\n')
        {
          break;
        }

      if (c == '\\' && !raw)
        {
          n = read(STDIN_FILENO, &c, 1);
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
    }

  /* Split into at most nvars fields; the last gets the remainder. */

  p = line.s != NULL ? line.s : "";
  for (k = 0; k < nvars; k++)
    {
      struct sbuf_s field;

      sb_init(&field);
      while (*p != '\0' && strchr(ifs, *p) != NULL && strchr(" \t\n", *p) != NULL)
        {
          p++;
        }

      if (k == nvars - 1)
        {
          const char *end = p + strlen(p);

          while (end > p && strchr(ifs, end[-1]) != NULL &&
                 strchr(" \t\n", end[-1]) != NULL)
            {
              end--;
            }

          while (p < end)
            {
              if (*p == '\\' && !raw && p + 1 < end)
                {
                  p++;
                }

              sb_addc(&field, *p++);
            }
        }
      else
        {
          while (*p != '\0' && strchr(ifs, *p) == NULL)
            {
              if (*p == '\\' && !raw && p[1] != '\0')
                {
                  p++;
                }

              sb_addc(&field, *p++);
            }

          if (*p != '\0')
            {
              p++;
            }
        }

      var_set(names[k], field.s != NULL ? field.s : "");
      sb_free(&field);
    }

  sb_free(&line);
  return eof ? 1 : 0;
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
                status = 1;
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
  int i;

  for (i = 1; i < argc; i++)
    {
      char *path;

      if (is_reserved_word(argv[i]))
        {
          printf("%s is a shell keyword\n", argv[i]);
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
            vs_err("type: %s: not found", argv[i]);
            status = 1;
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

static int bi_umask(int argc, char **argv)
{
  mode_t m;

  if (argc > 1)
    {
      char *end;
      long v = strtol(argv[1], &end, 8);

      if (*argv[1] == '\0' || *end != '\0' || v < 0 || v > 0777)
        {
          vs_err("umask: %s: invalid mode", argv[1]);
          return 1;
        }

      umask((mode_t)v);
      return 0;
    }

  m = umask(0);
  umask(m);
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
  { "false",    bi_false,    false, "do nothing, unsuccessfully", VS_M_ALL },
  { "help",     bi_help,     false, "help [name]: list builtins", VS_M_ALL },
  { "kill",     bi_kill,     false, "kill [-s sig | -sig] pid...: send a signal", VS_M_ALL },
  { "pwd",      bi_pwd,      false, "print the working directory", VS_M_ALL },
  { "read",     bi_read,     false, "read [-r] name...: read a line into variables", VS_M_ALL },
  { "test",     bi_test,     false, "test expr: evaluate a conditional expression", VS_M_ALL },
  { "true",     bi_true,     false, "do nothing, successfully", VS_M_ALL },
  { "type",     bi_type,     false, "type name...: say how a name resolves", VS_M_ALL },
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
