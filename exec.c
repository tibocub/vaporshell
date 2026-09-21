/*
 * exec.c -- runs an AST.
 *
 * Control flow (break/continue/return/exit) is not done with jumps: a
 * builtin sets g_sh.unwind and every loop and list checks it after each
 * command. set -e is a counter of "contexts where failure is expected"
 * (conditions, left sides of &&/||, negated pipelines).
 *
 * Anything needing a second process (subshell, pipeline stage, `&`,
 * command substitution) forks where the platform can; NuttX cannot yet,
 * see platform.h.
 */

#include <nuttx/config.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef VAPORSHELL_POSIX
#  include <regex.h>
#  include <sys/times.h>
#endif

#include "vaporshell.h"
#include "expand.h"
#include "exec.h"
#include "mode.h"
#include "platform.h"

#define MAX_FUNC_DEPTH 500

static int exec_list(struct node_s *list);
static int run_loop(struct parser_s *p, bool recover, int status);

/* ---- Helpers ------------------------------------------------------------------ */

/* bash's DEBUG trap: before each simple command, `for` iteration, `case`,
 * [[ and (( -- but not inside functions (that needs set -T).
 */

static void debug_hook(void)
{
  if (g_sh.trap_action[VS_TRAP_DEBUG] != NULL && g_sh.func_depth == 0)
    {
      trap_run_debug();
    }
}

static void errexit_check(int status)
{
  if (status != 0 && g_sh.noerrexit == 0 && g_sh.unwind == UW_NONE)
    {
      /* the ERR trap fires in exactly the situations -e would exit in */

      if (g_sh.trap_action[VS_TRAP_ERR] != NULL && g_sh.func_depth == 0)
        {
          trap_run_err(status);            /* not inherited by functions (no set -E) */
        }

      if (g_sh.opt_e && g_sh.unwind == UW_NONE)
        {
          g_sh.unwind = UW_EXIT;
          g_sh.last_status = status;
        }
    }
}

/* After a loop body/condition: true if the loop must stop. */

static bool loop_unwind(void)
{
  switch (g_sh.unwind)
    {
      case UW_NONE:
        return false;

      case UW_BREAK:
        if (--g_sh.unwind_count <= 0)
          {
            g_sh.unwind = UW_NONE;
          }

        return true;

      case UW_CONTINUE:
        if (--g_sh.unwind_count <= 0)
          {
            g_sh.unwind = UW_NONE;
            return false;
          }

        return true;

      default:
        return true;
    }
}

static int decode_status(int wstatus)
{
  if (WIFEXITED(wstatus))
    {
      return WEXITSTATUS(wstatus);
    }

  if (WIFSIGNALED(wstatus))
    {
      return 128 + WTERMSIG(wstatus);
    }

  return 1;
}

static int wait_for(pid_t pid)
{
  int wstatus;

  while (waitpid(pid, &wstatus, 0) < 0)
    {
      if (errno != EINTR)
        {
          return 1;
        }
    }

  return decode_status(wstatus);
}

/* In a forked child: run 'n', then leave without returning. */

static void child_run(struct node_s *n)
{
  int status;

  trap_reset_in_child();
  g_sh.can_exec = (n->type == N_SIMPLE);
  status = exec_node(n);
  if (g_sh.unwind != UW_NONE)
    {
      status = g_sh.last_status;
    }

  g_sh.unwind = UW_NONE;
  g_sh.last_status = status;
  trap_run_exit();
  fflush(NULL);
  _exit(g_sh.last_status & 0xff);
}

/* A special builtin's error ends a non-interactive shell (POSIX 2.8.1) --
 * in profiles that say so; bash's default carries on.
 */

void vs_special_error(void)
{
  /* dash does not let a failing special builtin end the shell from inside a
   * trap action; only that action is affected.
   */

  if (vs_feat(VF_SPECIAL_ERR_FATAL) && !g_sh.interactive &&
      g_sh.unwind == UW_NONE && g_sh.trap_depth == 0)
    {
      g_sh.unwind = UW_EXIT;
      g_sh.last_status = vs_feat(VF_EXIT2_ON_ERROR) ? 2 : 1;
    }
}

/* Runs a builtin and turns a failed write to stdout into a failure. */

static int call_builtin(const struct builtin_s *b, int argc, char **argv)
{
  int status = b->fn(argc, argv);

  if (fflush(stdout) != 0 || ferror(stdout))
    {
      if (status == 0)
        {
          status = 1;
        }

      vs_err("%s: write error: %s", argv[0], strerror(errno));
      clearerr(stdout);
    }

  return status;
}

static void xtrace(int argc, char **argv)
{
  int i;

  if (!g_sh.opt_x)
    {
      return;
    }

  fputs("+", stderr);
  for (i = 0; i < argc; i++)
    {
      fprintf(stderr, " %s", argv[i]);
    }

  fputc('\n', stderr);
}

/* ---- Command resolution and external programs ---------------------------------- */

enum cmd_kind_e classify_command(const char *name, char **path_out)
{
  const struct builtin_s *b = builtin_find(name);
  struct func_s *fn = func_find(name);
  int err;
  char *path;

  if (path_out != NULL)
    {
      *path_out = NULL;
    }

  if (b != NULL && b->special && (fn == NULL || vs_feat(VF_SPECIAL_BEFORE_FUNC)))
    {
      return CK_SPECIAL;
    }

  if (fn != NULL)
    {
      return CK_FUNCTION;
    }

  if (b != NULL)
    {
      return CK_BUILTIN;
    }

  path = vs_plat_find_command(name, var_get("PATH"), &err);
  if (path == NULL)
    {
      return CK_NONE;
    }

  if (path_out != NULL)
    {
      *path_out = path;
    }
  else
    {
      free(path);
    }

  return CK_EXTERNAL;
}

/* A file that is not a binary and has no #! line is a shell script: run
 * it with this shell (POSIX 2.9.1). Only where we can fork.
 */

static int run_as_script(const char *path, char **argv)
{
  pid_t pid;
  int i;
  int argc = 0;

  while (argv[argc] != NULL)
    {
      argc++;
    }

  pid = vs_plat_fork();
  if (pid < 0)
    {
      return 126;
    }

  if (pid == 0)
    {
      trap_reset_in_child();
      g_sh.arg0 = argv[0];
      for (i = 0; i < g_sh.npos; i++)
        {
          free(g_sh.pos[i]);
        }

      free(g_sh.pos);
      g_sh.pos = NULL;
      g_sh.npos = 0;
      pos_set(argv + 1, argc - 1);
      g_sh.interactive = false;
      g_sh.last_status = run_file(path);
      g_sh.unwind = UW_NONE;
      trap_run_exit();
      fflush(NULL);
      _exit(g_sh.last_status & 0xff);
    }

  return wait_for(pid);
}

static int run_external(char **argv, bool can_exec)
{
  int err = 0;
  char *path = hash_find_command(argv[0], var_get("PATH"), &err);
  char **envp;
  pid_t pid;
  int ret;
  int status;

  if (path == NULL)
    {
      if (err == EACCES)
        {
          vs_err("%s: Permission denied", argv[0]);
          return 126;
        }

      if (strchr(argv[0], '/') != NULL)
        {
          vs_err("%s: No such file or directory", argv[0]);
        }
      else
        {
          vs_err("%s: command not found", argv[0]);
        }

      return 127;
    }

  envp = var_build_env();
  fflush(NULL);

  if (can_exec && vs_plat_have_fork())
    {
      vs_plat_exec(path, argv, envp);
      if (errno == ENOEXEC)
        {
          g_sh.arg0 = argv[0];
          _exit(run_as_script(path, argv));
        }

      vs_err("%s: %s", argv[0], strerror(errno));
      _exit(errno == ENOENT ? 127 : 126);
    }

  ret = vs_plat_spawn(path, argv, envp, -1, -1, NULL, 0, &pid);
  if (ret == ENOEXEC && vs_plat_have_fork())
    {
      status = run_as_script(path, argv);
    }
  else if (ret != 0)
    {
      vs_err("%s: %s", argv[0], ret == ENOENT ? "command not found"
                                              : strerror(ret));
      status = (ret == ENOENT) ? 127 : 126;
    }
  else
    {
      status = wait_for(pid);
    }

  env_free(envp);
  free(path);
  return status;
}

static int call_function(struct func_s *f, int argc, char **argv)
{
  char **args;
  char **old;
  int old_n;
  int status;
  int i;
  int nargs = argc - 1;
  int saved_loops;
  struct frame_s frame;

  if (g_sh.func_depth >= MAX_FUNC_DEPTH)
    {
      vs_err("%s: function nesting too deep", argv[0]);
      return 1;
    }

  args = vs_xmalloc((size_t)(nargs > 0 ? nargs : 1) * sizeof(char *));
  for (i = 0; i < nargs; i++)
    {
      args[i] = vs_xstrdup(argv[i + 1]);
    }

  old = pos_swap(args, nargs, &old_n);
  g_sh.func_depth++;
  arena_retain(f->arena);

  /* break/continue are lexical: a loop outside the function is not one the
   * function can leave (bash and dash agree; measured in docs/modes.md).
   */

  saved_loops = g_sh.loop_depth;
  g_sh.loop_depth = 0;
  frame_push(&frame, f->name, f->src, g_sh.lineno, true);
  status = exec_node(f->body);
  frame_pop(&frame);
  g_sh.loop_depth = saved_loops;
  if (g_sh.unwind == UW_RETURN)
    {
      g_sh.unwind = UW_NONE;
      status = g_sh.last_status;
    }

  arena_release(f->arena);
  var_locals_pop(g_sh.func_depth);
  g_sh.func_depth--;
  /* RETURN is not inherited by functions: only one set inside this very
   * function runs when it returns.
   */

  if (g_sh.trap_action[VS_TRAP_RETURN] != NULL && g_sh.return_fdepth > 0 &&
      g_sh.func_depth == g_sh.return_fdepth - 1)
    {
      g_sh.last_status = status;
      trap_run_return();
    }

  for (i = 0; i < g_sh.npos; i++)
    {
      free(g_sh.pos[i]);
    }

  free(g_sh.pos);
  g_sh.pos = old;
  g_sh.npos = old_n;
  return status;
}

int run_argv(int argc, char **argv, bool skip_functions)
{
  const struct builtin_s *b = builtin_find(argv[0]);
  struct func_s *f = skip_functions ? NULL : func_find(argv[0]);
  int status;

  if (b != NULL &&
      (f == NULL || (b->special && vs_feat(VF_SPECIAL_BEFORE_FUNC))))
    {
      status = call_builtin(b, argc, argv);
      return status;
    }

  if (f != NULL)
    {
      return call_function(f, argc, argv);
    }

  return run_external(argv, false);
}

/* ---- Simple commands ---------------------------------------------------------------- */

struct tmpvar_s
{
  char *name;
  char *old;
  struct arr_s *old_arr;      /* the variable was an array: a copy, put back afterwards */
  bool had;
  unsigned flags;
};

/* A redirection that cannot be set up: 1 in bash, 2 in dash. */

static int redir_fail_status(void)
{
  return vs_feat(VF_EXIT2_ON_ERROR) ? 2 : 1;
}

static int exec_assign_only(struct node_s *n)
{
  struct redir_saved_s sv;
  struct word_s *w;
  int status = 0;

  if (n->redirs != NULL && redir_apply(n->redirs, &sv, false) != 0)
    {
      return redir_fail_status();
    }

  for (w = n->assigns; w != NULL && g_sh.unwind == UW_NONE; w = w->next)
    {
      if (assign_apply(w->text) != 0)
        {
          status = 1;
          vs_special_error();
        }
    }

  if (n->redirs != NULL)
    {
      redir_restore(&sv);
    }

  if (status == 0 && g_sh.cmdsub_status >= 0)
    {
      status = g_sh.cmdsub_status;
    }

  errexit_check(status);
  return status;
}

static void restore_tmpvars(struct tmpvar_s *tv, int ntv, bool keep)
{
  int i;
  bool locale = false;

  for (i = ntv - 1; i >= 0; i--)
    {
      struct var_s *v = var_lookup(tv[i].name);

      locale = locale || var_is_locale_var(tv[i].name);

      if (v != NULL)
        {
          if (keep)
            {
              v->flags = tv[i].flags;
            }
          else if (tv[i].old_arr != NULL)
            {
              arr_free(v->arr);            /* an array: element 0 was shadowed */
              v->arr = tv[i].old_arr;
              tv[i].old_arr = NULL;
              v->flags = tv[i].flags;
            }
          else if (tv[i].had)
            {
              free(v->value);
              v->value = tv[i].old;
              v->flags = tv[i].flags;
              tv[i].old = NULL;
            }
          else
            {
              v->flags &= ~(unsigned)VF_READONLY;
              var_unset(tv[i].name);
            }
        }

      free(tv[i].old);
      arr_free(tv[i].old_arr);
      free(tv[i].name);
    }

  if (locale)
    {
      vs_plat_locale_update();          /* `LC_ALL=C cmd` is over: back to the old collation */
    }

  free(tv);
}

/* declare, typeset, local, export and readonly take assignment-shaped arguments
 * that are not ordinary words: the value is not split or globbed, and an
 * array literal must reach the builtin unexpanded so its own quoting counts.
 */

static bool is_decl_command(const struct word_s *w)
{
  static const char *const names[] = { "declare", "typeset", "local", "export", "readonly", NULL };
  int i;

  if (!vs_feat(VF_BASH_SYNTAX) || w == NULL)
    {
      return false;
    }

  for (i = 0; names[i] != NULL; i++)
    {
      if (strcmp(w->text, names[i]) == 0 && func_find(names[i]) == NULL)
        {
          return true;
        }
    }

  return false;
}

static int expand_decl_words(const struct word_s *w, struct fieldv_s *argv,
                             unsigned long long *raw)
{
  struct word_s one;

  one = *w;
  one.next = NULL;
  if (expand_words(&one, argv) != 0)
    {
      return -1;
    }

  for (w = w->next; w != NULL; w = w->next)
    {
      if (asg_is_word(w->text))
        {
          bool is_raw = false;
          char *s = assign_decl_word(w->text, &is_raw);

          if (s == NULL)
            {
              return -1;
            }

          fv_add(argv, s);
          if (is_raw)
            {
              if (argv->n > 64)
                {
                  vs_err("too many arguments for an array assignment");
                  return -1;
                }

              *raw |= 1ULL << (argv->n - 1);
            }
        }
      else
        {
          one = *w;
          one.next = NULL;
          if (expand_words(&one, argv) != 0)
            {
              return -1;
            }
        }
    }

  return 0;
}

static int exec_simple(struct node_s *n)
{
  struct fieldv_s argv;
  unsigned long long rawmask = 0;
  struct tmpvar_s *tv = NULL;
  int ntv = 0;
  struct redir_saved_s sv;
  bool have_redirs = false;
  bool can_exec = g_sh.can_exec;
  const struct builtin_s *b;
  struct func_s *fn;
  struct word_s *w;
  int status = 0;
  bool keep_assign = false;

  g_sh.can_exec = false;
  g_sh.cmdsub_status = -1;
  if (n->line > 0)
    {
      g_sh.lineno = n->line;
    }

  debug_hook();

  fv_init(&argv);

  if ((is_decl_command(n->words) ? expand_decl_words(n->words, &argv, &rawmask)
                                 : expand_words(n->words, &argv)) != 0)
    {
      fv_free(&argv);
      return g_sh.unwind == UW_EXIT ? g_sh.last_status : 1;
    }

  if (argv.n > 0 && vs_feat(VF_BASH_VARS))
    {
      var_set("_", argv.v[argv.n - 1]);
    }

  if (argv.n == 0)
    {
      fv_free(&argv);
      return exec_assign_only(n);
    }

  b = builtin_find(argv.v[0]);

  if (n->redirs != NULL)
    {
      /* "exec >file" with no command keeps its redirections. */

      bool persist = (strcmp(argv.v[0], "exec") == 0 && argv.n == 1);

      if (redir_apply(n->redirs, &sv, persist) != 0)
        {
          status = redir_fail_status();
          if (b != NULL && b->special)
            {
              vs_special_error();
            }

          goto out;
        }

      have_redirs = !persist;
    }


  for (w = n->assigns; w != NULL; w = w->next)
    {
      char *name;
      const char *raw;
      char *val;
      bool append;
      struct var_s *v;

      if (!assign_scalar_parts(w->text, &name, &raw, &append))
        {
          vs_err("%s: an array assignment cannot precede a command", w->text);
          status = 1;
          goto out;
        }

      val = expand_assign_str(raw);
      if (val != NULL && append && var_get(name) != NULL)
        {
          char *both = vs_xmalloc(strlen(var_get(name)) + strlen(val) + 1);

          strcpy(both, var_get(name));
          strcat(both, val);
          free(val);
          val = both;
        }

      if (val == NULL)
        {
          free(name);
          status = 1;
          goto out;
        }

      tv = vs_xrealloc(tv, (size_t)(ntv + 1) * sizeof(*tv));
      v = var_lookup(name);
      tv[ntv].name = name;
      tv[ntv].had = (v != NULL && v->value != NULL);
      tv[ntv].old = tv[ntv].had ? vs_xstrdup(v->value) : NULL;
      tv[ntv].old_arr = (v != NULL && v->arr != NULL) ? arr_clone(v->arr) : NULL;
      tv[ntv].flags = v != NULL ? v->flags : 0;
      ntv++;

      if (var_set(name, val) != 0)
        {
          free(val);
          status = 1;
          goto out;
        }

      var_set_flags(name, VF_EXPORT);
      free(val);
    }

  xtrace(argv.n, argv.v);

  fn = func_find(argv.v[0]);
  if (b != NULL &&
      (fn == NULL || (b->special && vs_feat(VF_SPECIAL_BEFORE_FUNC))))
    {
      keep_assign = b->special && vs_feat(VF_SPECIAL_ASSIGN_KEEP);
      g_sh.decl_raw = rawmask;
      status = call_builtin(b, argv.n, argv.v);
      g_sh.decl_raw = 0;
    }
  else if (fn != NULL)
    {
      status = call_function(fn, argv.n, argv.v);
    }
  else
    {
      status = run_external(argv.v, can_exec && !have_redirs);
    }

out:
  restore_tmpvars(tv, ntv, keep_assign);
  if (have_redirs)
    {
      redir_restore(&sv);
    }

  fv_free(&argv);
  errexit_check(status);
  return status;
}

/* ---- Compound commands ------------------------------------------------------------------- */

static int exec_if(struct node_s *n)
{
  int c;

  g_sh.noerrexit++;
  c = exec_node(n->a);
  g_sh.noerrexit--;

  if (g_sh.unwind != UW_NONE)
    {
      return c;
    }

  if (c == 0)
    {
      return exec_node(n->b);
    }

  if (n->c != NULL)
    {
      return n->c->type == N_IF ? exec_if(n->c) : exec_node(n->c);
    }

  return 0;
}

static int exec_while(struct node_s *n)
{
  int status = 0;

  g_sh.loop_depth++;
  for (; ; )
    {
      int c;

      g_sh.noerrexit++;
      c = exec_node(n->a);
      g_sh.noerrexit--;
      if (g_sh.unwind != UW_NONE && loop_unwind())
        {
          break;
        }

      if (n->flag ? (c == 0) : (c != 0))
        {
          break;
        }

      status = exec_node(n->b);
      if (loop_unwind())
        {
          break;
        }
    }

  g_sh.loop_depth--;
  return status;
}

static int exec_for(struct node_s *n)
{
  struct fieldv_s items;
  int status = 0;
  int i;

  fv_init(&items);
  if (n->flag)
    {
      if (expand_words(n->words, &items) != 0)
        {
          fv_free(&items);
          return 1;
        }
    }
  else
    {
      for (i = 1; i <= g_sh.npos; i++)
        {
          fv_add(&items, vs_xstrdup(pos_get(i)));
        }
    }

  g_sh.loop_depth++;
  for (i = 0; i < items.n; i++)
    {
      debug_hook();                    /* bash: once per iteration */
      if (var_set(n->name, items.v[i]) != 0)
        {
          status = 1;
          break;
        }

      status = exec_node(n->a);
      if (loop_unwind())
        {
          break;
        }
    }

  g_sh.loop_depth--;
  fv_free(&items);
  return status;
}

static int exec_case(struct node_s *n)
{
  debug_hook();
  char *subject = expand_word_str(n->words->text);
  struct case_item_s *item;
  bool fall = false;
  int status = 0;

  if (subject == NULL)
    {
      return 1;
    }

  for (item = n->items; item != NULL; item = item->next)
    {
      struct word_s *w;
      bool matched = fall;

      fall = false;
      for (w = item->patterns; w != NULL && !matched; w = w->next)
        {
          struct pat_s pat = expand_pattern(w->text);

          matched = pat_match_ci(pat.s, pat.q, pat.len, subject, g_sh.so_nocasematch);
          pat_free(&pat);
        }

      if (matched)
        {
          status = exec_node(item->body);
          if (g_sh.unwind != UW_NONE)
            {
              break;
            }

          if (item->term == 1)
            {
              fall = true;             /* ;& : run the next clause unconditionally */
              continue;
            }

          if (item->term == 2)
            {
              continue;                /* ;;& : keep testing the following patterns */
            }

          break;
        }
    }

  free(subject);
  return status;
}

static int exec_subshell(struct node_s *n)
{
  pid_t pid;
  struct node_s *only = n->a;

  if (vs_inproc_enabled())
    {
      return vs_inproc_subshell(only);
    }

  pid = vs_plat_fork();
  if (pid < 0)
    {
      vs_err("( ): not supported on this platform yet");
      return 1;
    }

  if (pid == 0)
    {
      if (only->a != NULL && only->a->next == NULL && !only->a->async)
        {
          only = only->a;
        }

      child_run(only);
    }

  return wait_for(pid);
}

/* Runs 'fn' with the node's own redirections in effect. */

static int with_redirs(struct node_s *n, int (*fn)(struct node_s *))
{
  struct redir_saved_s sv;
  int status;

  if (n->redirs == NULL)
    {
      return fn(n);
    }

  if (redir_apply(n->redirs, &sv, false) != 0)
    {
      return redir_fail_status();
    }

  status = fn(n);
  redir_restore(&sv);
  return status;
}

static int exec_brace(struct node_s *n)
{
  return exec_node(n->a);
}

/* ---- Pipelines ------------------------------------------------------------------------------ */

static int count_stages(struct node_s *first)
{
  int n = 0;

  for (; first != NULL; first = first->next)
    {
      n++;
    }

  return n;
}

#ifdef VAPORSHELL_POSIX

static int run_stages(struct node_s *first, int nst)
{
  int (*pipes)[2] = vs_xmalloc((size_t)nst * sizeof(*pipes));
  pid_t *pids = vs_xmalloc((size_t)nst * sizeof(pid_t));
  struct node_s *stage = first;
  int status = 1;
  int failed = 0;
  int i;
  int k;

  for (i = 0; i < nst - 1; i++)
    {
      if (pipe(pipes[i]) != 0)
        {
          vs_err("pipe: %s", strerror(errno));
          while (--i >= 0)
            {
              close(pipes[i][0]);
              close(pipes[i][1]);
            }

          free(pipes);
          free(pids);
          return 1;
        }
    }

  for (i = 0; i < nst; i++, stage = stage->next)
    {
      pids[i] = vs_plat_fork();
      if (pids[i] == 0)
        {
          if (i > 0)
            {
              dup2(pipes[i - 1][0], STDIN_FILENO);
            }

          if (i < nst - 1)
            {
              dup2(pipes[i][1], STDOUT_FILENO);
            }

          for (k = 0; k < nst - 1; k++)
            {
              close(pipes[k][0]);
              close(pipes[k][1]);
            }

          child_run(stage);
        }
    }

  for (k = 0; k < nst - 1; k++)
    {
      close(pipes[k][0]);
      close(pipes[k][1]);
    }

  for (i = 0; i < nst; i++)
    {
      int s = pids[i] > 0 ? wait_for(pids[i]) : 1;

      ps_record(i, s);

      if (s != 0)
        {
          failed = s;
        }

      if (i == nst - 1)
        {
          status = s;
        }
    }

  if (g_sh.opt_pipefail && failed != 0)
    {
      status = failed;               /* rightmost failing stage */
    }

  free(pipes);
  free(pids);
  return status;
}

#else

/* No fork: every stage has to be a plain external program, started with
 * its stdin/stdout wired to the pipes.
 */

static int run_stages(struct node_s *first, int nst)
{
  int (*pipes)[2] = vs_xmalloc((size_t)nst * sizeof(*pipes));
  int *closes = vs_xmalloc((size_t)(2 * nst) * sizeof(int));
  pid_t *pids = vs_xmalloc((size_t)nst * sizeof(pid_t));
  struct node_s *stage = first;
  int status = 1;
  int failed = 0;
  int i;
  int nclose = 0;

  for (i = 0; i < nst - 1; i++)
    {
      if (pipe(pipes[i]) != 0)
        {
          vs_err("pipe: %s", strerror(errno));
          free(pipes);
          free(closes);
          free(pids);
          return 1;
        }

      closes[nclose++] = pipes[i][0];
      closes[nclose++] = pipes[i][1];
    }

  for (i = 0; i < nst; i++, stage = stage->next)
    {
      struct fieldv_s argv;
      char *path;
      char **envp;
      int err;

      pids[i] = -1;
      fv_init(&argv);
      if (stage->type != N_SIMPLE || stage->redirs != NULL ||
          stage->assigns != NULL || expand_words(stage->words, &argv) != 0 ||
          argv.n == 0 ||
          (builtin_find(argv.v[0]) != NULL &&
           !vs_plat_external_fallback(argv.v[0])) ||
          func_find(argv.v[0]) != NULL)
        {
          vs_err("pipelines can only run external programs on this "
                 "platform yet");
          fv_free(&argv);
          continue;
        }

      path = hash_find_command(argv.v[0], var_get("PATH"), &err);
      envp = var_build_env();
      if (path == NULL ||
          vs_plat_spawn(path, argv.v, envp, i > 0 ? pipes[i - 1][0] : -1,
                        i < nst - 1 ? pipes[i][1] : -1, closes, nclose,
                        &pids[i]) != 0)
        {
          vs_err("%s: command not found", argv.v[0]);
          pids[i] = -1;
        }

      env_free(envp);
      free(path);
      fv_free(&argv);
    }

  for (i = 0; i < nclose; i++)
    {
      close(closes[i]);
    }

  for (i = 0; i < nst; i++)
    {
      int s = pids[i] > 0 ? wait_for(pids[i]) : 127;

      ps_record(i, s);

      if (s != 0)
        {
          failed = s;
        }

      if (i == nst - 1)
        {
          status = s;
        }
    }

  if (g_sh.opt_pipefail && failed != 0)
    {
      status = failed;               /* rightmost failing stage */
    }

  free(pipes);
  free(closes);
  free(pids);
  return status;
}

#endif

/* ---- Pipelines without fork ------------------------------------------------
 *
 * A stage that is a plain external program (a literal name, no redirection,
 * not a builtin or function) is spawned and streams as it would anywhere.
 * Every other stage runs in-process as a subshell. When an in-process stage
 * feeds the next one its output is captured and handed over by a helper
 * thread (inproc.c), so nothing can deadlock on a full pipe. The one
 * difference from a real pipeline: an in-process stage runs to completion
 * before the stage after it starts reading, so an in-process producer that
 * never ends (`while true; do echo y; done | head -1`) does not terminate.
 */

static bool is_literal_word(const char *t)
{
  for (; *t != '\0'; t++)
    {
      if (!((*t >= 'a' && *t <= 'z') || (*t >= 'A' && *t <= 'Z') ||
            (*t >= '0' && *t <= '9') || strchr("_./+:@%-", *t) != NULL))
        {
          return false;
        }
    }

  return true;
}

static bool stage_is_plain_external(const struct node_s *st)
{
  const char *name;

  if (st->type != N_SIMPLE || st->redirs != NULL || st->assigns != NULL ||
      st->words == NULL)
    {
      return false;
    }

  name = st->words->text;
  return is_literal_word(name) && func_find(name) == NULL &&
         (builtin_find(name) == NULL || vs_plat_external_fallback(name));
}

static void cloexec(int fd)
{
#ifdef FD_CLOEXEC
  fcntl(fd, F_SETFD, FD_CLOEXEC);
#else
  (void)fd;
#endif
}

static int run_stages_inproc(struct node_s *first, int nst)
{
  pid_t *pids = vs_xmalloc((size_t)nst * sizeof(pid_t));
  int *stat_of = vs_xmalloc((size_t)nst * sizeof(int));
  struct node_s *stage = first;
  int in_fd = -1;
  int failed = 0;
  int status = 1;
  int i;

  for (i = 0; i < nst; i++, stage = stage->next)
    {
      bool last = (i == nst - 1);
      int pp[2] = { -1, -1 };

      pids[i] = -1;
      stat_of[i] = 0;

      if (!last && pipe(pp) != 0)
        {
          vs_err("pipe: %s", strerror(errno));
          stat_of[i] = 1;
          break;
        }

      if (pp[0] >= 0)
        {
          cloexec(pp[0]);
          cloexec(pp[1]);
        }

      if (stage_is_plain_external(stage))
        {
          struct fieldv_s argv;
          char *path = NULL;
          char **envp;
          int err = 0;
          int closes[2];
          int nc = 0;

          fv_init(&argv);
          if (expand_words(stage->words, &argv) != 0 || argv.n == 0)
            {
              stat_of[i] = 1;
            }
          else
            {
              path = hash_find_command(argv.v[0], var_get("PATH"), &err);
              envp = var_build_env();
              if (pp[0] >= 0)
                {
                  closes[nc++] = pp[0];         /* the reader end is ours */
                }

              if (path == NULL ||
                  vs_plat_spawn(path, argv.v, envp, in_fd, pp[1], closes, nc,
                                &pids[i]) != 0)
                {
                  vs_err("%s: command not found", argv.v[0]);
                  pids[i] = -1;
                  stat_of[i] = 127;
                }

              env_free(envp);
              free(path);
            }

          fv_free(&argv);
          if (pp[1] >= 0)
            {
              close(pp[1]);
            }

          if (in_fd >= 0)
            {
              close(in_fd);
            }

          in_fd = pp[0];
        }
      else
        {
          char *captured = NULL;

          stat_of[i] = vs_inproc_stage(stage, in_fd, last ? NULL : &captured);
          if (in_fd >= 0)
            {
              close(in_fd);
              in_fd = -1;
            }

          if (!last)
            {
              /* pp[0] becomes the next stage's stdin; a thread fills it. */

              vs_inproc_feed(pp[1], captured != NULL ? captured : vs_xstrdup(""),
                             captured != NULL ? strlen(captured) : 0);
              in_fd = pp[0];
            }
        }
    }

  if (in_fd >= 0)
    {
      close(in_fd);
    }

  for (i = 0; i < nst; i++)
    {
      int s = pids[i] > 0 ? wait_for(pids[i]) : stat_of[i];

      ps_record(i, s);

      if (s != 0)
        {
          failed = s;
        }

      if (i == nst - 1)
        {
          status = s;
        }
    }

  if (g_sh.opt_pipefail && failed != 0)
    {
      status = failed;
    }

  free(pids);
  free(stat_of);
  return status;
}

static int exec_pipeline(struct node_s *n)
{
  int nst = count_stages(n->a);
  int status;

  if (n->flag)
    {
      g_sh.noerrexit++;
    }

  if (nst == 1)
    {
      status = exec_node(n->a);
    }
  else if (vs_inproc_enabled())
    {
      status = run_stages_inproc(n->a, nst);
    }
  else
    {
      status = run_stages(n->a, nst);
    }

  if (n->flag)
    {
      g_sh.noerrexit--;
      return status == 0 ? 1 : 0;
    }

  errexit_check(status);
  return status;
}

/* ---- Lists ------------------------------------------------------------------------------------- */

/* `cmd args &` without fork(): a plain external program is spawned and not
 * waited for; $! is its pid and `wait` collects it. Anything that would have
 * to run inside the shell (a builtin, function, compound command) cannot run
 * concurrently with the shell itself, so it is refused rather than run in the
 * foreground behind the user's back.
 */

static int exec_async_spawn(struct node_s *st)
{
  struct fieldv_s argv;
  char *path;
  char **envp;
  int err = 0;
  int in_fd = -1;
  pid_t pid = -1;
  int ret;

  fv_init(&argv);
  if (expand_words(st->words, &argv) != 0 || argv.n == 0)
    {
      fv_free(&argv);
      return 1;
    }

  path = hash_find_command(argv.v[0], var_get("PATH"), &err);
  if (path == NULL)
    {
      vs_err("%s: command not found", argv.v[0]);
      fv_free(&argv);
      return 127;
    }

  if (!g_sh.interactive)
    {
      in_fd = open("/dev/null", O_RDONLY);    /* a background job has no stdin */
    }

  envp = var_build_env();
  fflush(NULL);
  ret = vs_plat_spawn(path, argv.v, envp, in_fd, -1, NULL, 0, &pid);
  if (in_fd >= 0)
    {
      close(in_fd);
    }

  env_free(envp);
  free(path);
  fv_free(&argv);
  if (ret != 0)
    {
      vs_err("&: %s", strerror(ret));
      return 126;
    }

  g_sh.last_bg = pid;
  return 0;
}

static int exec_async(struct node_s *n)
{
  pid_t pid = vs_plat_fork();

  if (pid < 0)
    {
      struct node_s *st = (n->type == N_PIPE && !n->flag && n->a != NULL &&
                           n->a->next == NULL) ? n->a : n;

      if (stage_is_plain_external(st))
        {
          return exec_async_spawn(st);
        }

      vs_err("&: only an external program can run in the background on this "
             "platform (no fork)");
      return 1;
    }

  if (pid == 0)
    {
      if (!g_sh.interactive)
        {
          int fd = open("/dev/null", O_RDONLY);

          if (fd >= 0)
            {
              dup2(fd, STDIN_FILENO);
              close(fd);
            }
        }

      child_run(n);
    }

  g_sh.last_bg = pid;
  return 0;
}

static int exec_list(struct node_s *list)
{
  struct node_s *item;
  int status = 0;

  for (item = list->a; item != NULL; item = item->next)
    {
      /* item->next is also how a pipeline chains its stages, but list
       * items are the heads, so it is safe to follow here.
       */

      status = item->async ? exec_async(item) : exec_node(item);
      if (g_sh.unwind == UW_NONE)
        {
          g_sh.last_status = status;
          if (g_sh.trap_pending)
            {
              trap_run_pending();
            }
        }
      else
        {
          break;
        }
    }

  return status;
}

/* ---- Bash compound commands: [[ ]], (( )), for (( )), time ------------------- */

#ifdef VAPORSHELL_POSIX
/* subject =~ regex, POSIX ERE. Characters that were quoted in the pattern
 * match literally, as in bash. 0 match, 1 no match, 2 bad expression.
 */

static int db_regex(const char *subject, const char *raw)
{
  struct pat_s pat = expand_pattern(raw);
  struct sbuf_s re;
  regex_t rx;
  size_t i;
  int rc;

  sb_init(&re);
  for (i = 0; i < pat.len; i++)
    {
      if (pat.q != NULL && pat.q[i] && strchr(".[]{}()*+?|^$\\", pat.s[i]) != NULL)
        {
          sb_addc(&re, '\\');
        }

      sb_addc(&re, pat.s[i]);
    }

  pat_free(&pat);
  rc = regcomp(&rx, re.s != NULL ? re.s : "", REG_EXTENDED | (g_sh.so_nocasematch ? REG_ICASE : 0));
  sb_free(&re);
  if (rc != 0)
    {
      return 2;
    }

  {
    /* BASH_REMATCH: the whole match and each group; a group that did not take
     * part is an empty element. A failed match leaves it empty.
     */

    size_t ng = rx.re_nsub + 1;
    regmatch_t *m = vs_xmalloc(ng * sizeof(*m));
    struct arr_s *a = arr_new();

    rc = regexec(&rx, subject, ng, m, 0);
    if (rc == 0)
      {
        size_t g;

        for (g = 0; g < ng; g++)
          {
            char *piece = m[g].rm_so >= 0
                          ? vs_xstrndup(subject + m[g].rm_so, (size_t)(m[g].rm_eo - m[g].rm_so))
                          : vs_xstrdup("");

            arr_set(a, (long)g, piece);
            free(piece);
          }
      }

    var_array_replace("BASH_REMATCH", a);
    free(m);
  }

  regfree(&rx);
  return rc == 0 ? 0 : 1;
}
#endif

static int db_arith(const char *raw, long *v)
{
  char *e = expand_word_str(raw);
  int r = 0;

  if (e == NULL)
    {
      return -1;
    }

  *v = 0;
  if (e[0] != '\0')
    {
      r = arith_eval(e, v);
    }

  free(e);
  return r;
}

static bool is_arith_cmp(const char *op)
{
  return op[0] == '-' && strlen(op) == 3 && strchr("enlg", op[1]) != NULL &&
         (strcmp(op, "-eq") == 0 || strcmp(op, "-ne") == 0 ||
          strcmp(op, "-lt") == 0 || strcmp(op, "-le") == 0 ||
          strcmp(op, "-gt") == 0 || strcmp(op, "-ge") == 0);
}

/* One test inside [[ ]]: 0 true, 1 false, 2 error. */

static int db_test(struct node_s *n)
{
  const char *op = n->name;
  struct word_s *w = n->words;
  char *a = NULL;
  char *b = NULL;
  int r = 2;

  if (op[0] == '\0')
    {
      a = expand_word_str(w->text);
      r = (a != NULL) ? (a[0] != '\0' ? 0 : 1) : 2;
      free(a);
      return r;
    }

  if (w->next == NULL)
    {
      char *argv[3];

      a = expand_word_str(w->text);
      if (a == NULL)
        {
          return 2;
        }

      if (strcmp(op, "-v") == 0)
        {
          r = asg_ref_isset(a) ? 0 : 1;
        }
      else if (strcmp(op, "-o") == 0)
        {
          r = vs_option_state(a) == 1 ? 0 : 1;
        }
      else if (strcmp(op, "-R") == 0)
        {
          r = 1;                      /* namerefs: not implemented */
        }
      else
        {
          argv[0] = "test";
          argv[1] = strcmp(op, "-a") == 0 ? "-e" : (char *)op;
          argv[2] = a;
          r = bi_test(3, argv);
        }

      free(a);
      return r;
    }

  if (is_arith_cmp(op))
    {
      long x;
      long y;

      if (db_arith(w->text, &x) != 0 || db_arith(w->next->text, &y) != 0)
        {
          return 2;
        }

      switch (op[1])
        {
          case 'e': return x == y ? 0 : 1;
          case 'n': return x != y ? 0 : 1;
          case 'l': return (op[2] == 't' ? x < y : x <= y) ? 0 : 1;
          default:  return (op[2] == 't' ? x > y : x >= y) ? 0 : 1;
        }
    }

  a = expand_word_str(w->text);
  if (a == NULL)
    {
      return 2;
    }

  if (strcmp(op, "==") == 0 || strcmp(op, "=") == 0 || strcmp(op, "!=") == 0)
    {
      struct pat_s pat = expand_pattern(w->next->text);
      bool ext = g_sh.so_extglob;
      bool m;

      g_sh.so_extglob = true;                  /* always on inside [[ ]] */
      m = pat_match_ci(pat.s, pat.q, pat.len, a, g_sh.so_nocasematch);
      g_sh.so_extglob = ext;
      pat_free(&pat);
      free(a);
      return (m == (op[0] != '!')) ? 0 : 1;
    }

  if (strcmp(op, "=~") == 0)
    {
#ifdef VAPORSHELL_POSIX
      r = db_regex(a, w->next->text);
      if (r == 2)
        {
          vs_err("[[: invalid regular expression");
        }
#else
      vs_err("[[ =~ ]]: not supported on this platform yet");
      r = 2;
#endif
      free(a);
      return r;
    }

  b = expand_word_str(w->next->text);
  if (b == NULL)
    {
      free(a);
      return 2;
    }

  if (strcmp(op, "<") == 0)
    {
      r = vs_plat_collate(a, b) < 0 ? 0 : 1;      /* [[ ]] sorts by the locale */
    }
  else if (strcmp(op, ">") == 0)
    {
      r = vs_plat_collate(a, b) > 0 ? 0 : 1;
    }
  else
    {
      char *argv[4];

      argv[0] = "test";
      argv[1] = a;
      argv[2] = (char *)op;         /* -nt -ot -ef */
      argv[3] = b;
      r = bi_test(4, argv);
    }

  free(a);
  free(b);
  return r;
}

static int db_eval(struct node_s *n)
{
  int r;

  switch (n->type)
    {
      case N_DB_AND:
        r = db_eval(n->a);
        return r == 0 ? db_eval(n->b) : r;

      case N_DB_OR:
        r = db_eval(n->a);
        return r == 1 ? db_eval(n->b) : r;

      case N_DB_NOT:
        r = db_eval(n->a);
        return r == 2 ? 2 : (r == 0 ? 1 : 0);

      default:
        return db_test(n);
    }
}

static int exec_dbracket(struct node_s *n)
{
  debug_hook();
  return db_eval(n->a);
}

/* (( expr )): status 0 if the value is non-zero. */

static int arith_status(const char *raw, bool *err)
{
  char *e = expand_heredoc(raw);
  long v = 0;
  int r;

  *err = false;
  if (e == NULL)
    {
      *err = true;
      return 1;
    }

  r = arith_eval(e, &v);
  free(e);
  if (r != 0)
    {
      *err = true;
      return 1;
    }

  return v != 0 ? 0 : 1;
}

static int exec_arith(struct node_s *n)
{
  bool err;

  debug_hook();
  return arith_status(n->words->text, &err);
}

static bool blank_text(const char *s)
{
  for (; *s != '\0'; s++)
    {
      if (*s != ' ' && *s != '\t' && *s != '\n')
        {
          return false;
        }
    }

  return true;
}

static int exec_arithfor(struct node_s *n)
{
  struct word_s *init = n->words;
  struct word_s *cond = init->next;
  struct word_s *step = cond->next;
  int status = 0;
  bool err;

  if (!blank_text(init->text))
    {
      arith_status(init->text, &err);
      if (err)
        {
          return 1;
        }
    }

  g_sh.loop_depth++;
  for (; ; )
    {
      if (!blank_text(cond->text))
        {
          int c = arith_status(cond->text, &err);

          if (err || c != 0)
            {
              if (err)
                {
                  status = 1;
                }

              break;
            }
        }

      status = exec_node(n->a);
      if (loop_unwind())
        {
          break;
        }

      if (!blank_text(step->text))
        {
          arith_status(step->text, &err);
          if (err)
            {
              status = 1;
              break;
            }
        }
    }

  g_sh.loop_depth--;
  return status;
}

/* time [-p] pipeline: real/user/sys to stderr, in TIMEFORMAT (or bash's or
 * POSIX's default format).
 */

static void time_print(bool posix, double real, double user, double sys)
{
  const char *fmt = var_get("TIMEFORMAT");
  const char *p;

  if (fmt == NULL)
    {
      fmt = posix ? "real %2R\nuser %2U\nsys %2S"
                  : "\nreal\t%3lR\nuser\t%3lU\nsys\t%3lS";
    }
  else if (fmt[0] == '\0')
    {
      return;
    }

  for (p = fmt; *p != '\0'; p++)
    {
      int prec = 3;
      bool longf = false;
      double v;

      if (*p != '%')
        {
          putc(*p, stderr);
          continue;
        }

      p++;
      if (*p == '%')
        {
          putc('%', stderr);
          continue;
        }

      if (*p >= '0' && *p <= '9')
        {
          prec = *p++ - '0';
        }

      if (*p == 'l')
        {
          longf = true;
          p++;
        }

      switch (*p)
        {
          case 'R': v = real; break;
          case 'U': v = user; break;
          case 'S': v = sys; break;
          case 'P': fprintf(stderr, "%.*f", prec > 3 ? 3 : prec,
                            real > 0 ? (user + sys) * 100.0 / real : 0.0);
                    continue;
          default:  putc('%', stderr); p--; continue;
        }

      if (longf)
        {
          int m = (int)(v / 60.0);

          fprintf(stderr, "%dm%.*fs", m, prec, v - m * 60.0);
        }
      else
        {
          fprintf(stderr, "%.*f", prec, v);
        }
    }

  putc('\n', stderr);
}

static int exec_time(struct node_s *n)
{
  struct timespec t0;
  struct timespec t1;
  double user = 0.0;
  double sys = 0.0;
  int status;

#ifdef VAPORSHELL_POSIX
  struct tms c0;
  struct tms c1;
  long hz = sysconf(_SC_CLK_TCK);

  times(&c0);
#endif
  clock_gettime(CLOCK_MONOTONIC, &t0);
  status = exec_node(n->a);
  clock_gettime(CLOCK_MONOTONIC, &t1);
#ifdef VAPORSHELL_POSIX
  times(&c1);
  if (hz > 0)
    {
      user = (double)(c1.tms_utime - c0.tms_utime + c1.tms_cutime - c0.tms_cutime) / (double)hz;
      sys = (double)(c1.tms_stime - c0.tms_stime + c1.tms_cstime - c0.tms_cstime) / (double)hz;
    }
#endif

  time_print(n->flag,
             (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9,
             user, sys);
  return status;
}

int exec_node(struct node_s *n)
{
  int status = 0;

  if (g_sh.opt_n && !g_sh.interactive)
    {
      return 0;                    /* set -n: read, do not run */
    }

  switch (n->type)
    {
      case N_SIMPLE:
        status = exec_simple(n);
        break;

      case N_PIPE:
        status = exec_pipeline(n);
        break;

      case N_AND:
      case N_OR:
        g_sh.noerrexit++;
        status = exec_node(n->a);
        g_sh.noerrexit--;
        if (g_sh.unwind == UW_NONE)
          {
            g_sh.last_status = status;
            if ((n->type == N_AND) == (status == 0))
              {
                status = exec_node(n->b);
              }
          }

        break;

      case N_LIST:
        status = exec_list(n);
        break;

      case N_SUBSHELL:
        status = with_redirs(n, exec_subshell);
        errexit_check(status);
        break;

      case N_BRACE:
        status = with_redirs(n, exec_brace);
        break;

      case N_IF:
        status = with_redirs(n, exec_if);
        break;

      case N_WHILE:
        status = with_redirs(n, exec_while);
        break;

      case N_FOR:
        status = with_redirs(n, exec_for);
        break;

      case N_CASE:
        status = with_redirs(n, exec_case);
        break;

      case N_FUNCDEF:
        func_define(n->name, n->a, n->arena);
        break;

      case N_DBRACKET:
        status = with_redirs(n, exec_dbracket);
        break;

      case N_ARITH:
        status = with_redirs(n, exec_arith);
        errexit_check(status);
        break;

      case N_ARITHFOR:
        status = with_redirs(n, exec_arithfor);
        break;

      case N_TIME:
        status = exec_time(n);
        break;

      case N_DB_AND:
      case N_DB_OR:
      case N_DB_NOT:
      case N_DB_TEST:
        break;                       /* only ever evaluated by db_eval() */
    }

  /* PIPESTATUS is the statuses of the last pipeline that ran: a real pipeline
   * records its stages itself, a leaf command is a pipeline of one, and a
   * compound command adds nothing (`while false; do :; done` leaves the 1 of
   * its condition)
   */

  if (n->type == N_SIMPLE || n->type == N_SUBSHELL || n->type == N_DBRACKET ||
      n->type == N_ARITH || n->type == N_FUNCDEF)
    {
      ps_single(status);
    }

  if (g_sh.unwind == UW_NONE)
    {
      g_sh.last_status = status;
    }

  return status;
}

/* Like run_string(), for a command substitution's own process: when the
 * whole text is one simple command, let it replace the process (so a
 * program run as $(prog) is a direct child, as in other shells).
 */

static int run_string_child(const char *text, size_t len)
{
  struct parser_s p;
  struct arena_s *arena;
  struct node_s *node;
  int status = 0;

  parser_init_mem(&p, text, len);
  if (parse_command_line(&p, &arena, &node) == PARSE_OK && node != NULL)
    {
      struct node_s *only = node->a;

      if (only != NULL && only->next == NULL && !only->async &&
          only->type == N_SIMPLE && p.lx.pos >= p.lx.len)
        {
          g_sh.can_exec = true;
        }

      status = exec_node(node);
      if (g_sh.unwind == UW_NONE)
        {
          status = run_loop(&p, false, status);
        }
    }
  else
    {
      status = run_loop(&p, false, 0);
    }

  arena_release(arena);
  parser_free(&p);
  return status;
}

/* ---- Command substitution ----------------------------------------------------------------------- */

char *run_cmdsub(const char *text, size_t len)
{
  struct sbuf_s out;
  int fds[2];
  pid_t pid;
  char buf[512];
  ssize_t n;
  int status;

  sb_init(&out);
  fflush(NULL);

  if (vs_inproc_enabled())
    {
      char *r = vs_inproc_cmdsub(text, len, &status);

      g_sh.cmdsub_status = status;
      out.s = r != NULL ? r : vs_xstrdup("");
      out.len = strlen(out.s);
      out.cap = out.len + 1;
      goto strip;
    }

  if (!vs_plat_have_fork())
    {
      /* 'text' is a slice of a word, not a C string: hand the platform
       * layer a terminated copy of exactly 'len' bytes.
       */

      char *copy = vs_xstrndup(text, len);
      char *r = vs_plat_capture_via_self(copy, &status);

      free(copy);
      g_sh.cmdsub_status = status;
      if (r == NULL)
        {
          return vs_xstrdup("");
        }

      out.s = r;
      out.len = strlen(r);
      out.cap = out.len + 1;
      goto strip;
    }

  if (pipe(fds) != 0)
    {
      vs_err("pipe: %s", strerror(errno));
      return vs_xstrdup("");
    }

  pid = vs_plat_fork();
  if (pid < 0)
    {
      close(fds[0]);
      close(fds[1]);
      return vs_xstrdup("");
    }

  if (pid == 0)
    {
      close(fds[0]);
      dup2(fds[1], STDOUT_FILENO);
      close(fds[1]);
      g_sh.interactive = false;
      if (!vs_feat(VF_ERREXIT_IN_CMDSUB))
        {
          g_sh.opt_e = false;    /* bash's default: $(...) does not inherit -e */
        }

      trap_reset_in_child();
      status = run_string_child(text, len);
      if (g_sh.unwind != UW_NONE)
        {
          status = g_sh.last_status;
        }

      g_sh.unwind = UW_NONE;
      g_sh.last_status = status;
      trap_run_exit();
      fflush(NULL);
      _exit(g_sh.last_status & 0xff);
    }

  close(fds[1]);
  for (; ; )
    {
      n = read(fds[0], buf, sizeof(buf));
      if (n > 0)
        {
          sb_addn(&out, buf, (size_t)n);
        }
      else if (n == 0 || errno != EINTR)
        {
          break;
        }
    }

  close(fds[0]);
  g_sh.cmdsub_status = wait_for(pid);

strip:
  while (out.len > 0 && out.s[out.len - 1] == '\n')
    {
      out.s[--out.len] = '\0';
    }

  return sb_take(&out);
}

/* ---- Driving the parser ---------------------------------------------------------------------------- */

static int run_loop(struct parser_s *p, bool recover, int status)
{
  for (; ; )
    {
      struct arena_s *arena;
      struct node_s *node;
      enum parse_result_e r = parse_command_line(p, &arena, &node);

      if (r == PARSE_EOF)
        {
          arena_release(arena);
          break;
        }

      if (r == PARSE_ERR)
        {
          vs_err("%s", p->err);
          g_sh.syntax_error = true;
          status = 2;
          g_sh.last_status = 2;
          arena_release(arena);
          if (!recover)
            {
              break;
            }

          parser_discard(p);
          continue;
        }

      if (node != NULL && !(g_sh.opt_n && !g_sh.interactive))
        {
          status = exec_node(node);
        }

      arena_release(arena);
      if (g_sh.unwind != UW_NONE)
        {
          status = g_sh.last_status;
          break;
        }
    }

  return status;
}

int run_source(struct parser_s *p, bool recover)
{
  return run_loop(p, recover, 0);
}

int run_string(const char *text, size_t len)
{
  struct parser_s p;
  int status;

  parser_init_mem(&p, text, len);
  if (g_sh.lineno > 1)
    {
      p.lx.line_base = g_sh.lineno - 1;   /* eval'd text counts from its caller's line */
    }

  status = run_source(&p, false);
  parser_free(&p);
  return status;
}

char *read_stream_line(FILE *fp)
{
  struct sbuf_s line;
  char chunk[256];

  sb_init(&line);
  while (fgets(chunk, sizeof(chunk), fp) != NULL)
    {
      size_t n = strlen(chunk);

      sb_addn(&line, chunk, n);
      if (n > 0 && chunk[n - 1] == '\n')
        {
          break;
        }
    }

  if (g_sh.opt_v && line.len > 0)
    {
      fwrite(line.s, 1, line.len, stderr);      /* set -v */
    }

  return line.len > 0 ? sb_take(&line) : (sb_free(&line), NULL);
}

static char *file_next_line(void *ctx, bool continuation)
{
  (void)continuation;
  return read_stream_line((FILE *)ctx);
}

int run_file(const char *path)
{
  char pbuf[VS_PATH_MAX];
  FILE *fp = fopen(vs_plat_fspath(path, pbuf, sizeof(pbuf)), "r");
  struct parser_s p;
  int status;

  if (fp == NULL)
    {
      vs_err("%s: %s", path, strerror(errno));
      return 127;
    }

  /* Move the script to a high descriptor: it would otherwise be fd 3, and
   * `exec 3>file` in the script would silently replace the script itself
   * (bash and dash both do this).
   */

  {
    int hi = vs_plat_dup_high(fileno(fp));
    FILE *hfp = hi >= 0 ? fdopen(hi, "r") : NULL;

    if (hfp != NULL)
      {
        fclose(fp);
        fp = hfp;
      }
    else if (hi >= 0)
      {
        close(hi);
      }
  }

  parser_init(&p, file_next_line, fp);
  status = run_source(&p, false);
  parser_free(&p);
  fclose(fp);
  return status;
}
