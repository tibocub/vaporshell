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
#include <unistd.h>

#include "vaporshell.h"
#include "expand.h"
#include "exec.h"
#include "mode.h"
#include "platform.h"

#define MAX_FUNC_DEPTH 500

static int exec_list(struct node_s *list);
static int run_loop(struct parser_s *p, bool recover, int status);

/* ---- Helpers ------------------------------------------------------------------ */

static void errexit_check(int status)
{
  if (status != 0 && g_sh.opt_e && g_sh.noerrexit == 0 &&
      g_sh.unwind == UW_NONE)
    {
      g_sh.unwind = UW_EXIT;
      g_sh.last_status = status;
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
  if (vs_feat(VF_SPECIAL_ERR_FATAL) && !g_sh.interactive &&
      g_sh.unwind == UW_NONE)
    {
      g_sh.unwind = UW_EXIT;
      g_sh.last_status = 1;
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
  char *path = vs_plat_find_command(argv[0], var_get("PATH"), &err);
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
  status = exec_node(f->body);
  g_sh.loop_depth = saved_loops;
  if (g_sh.unwind == UW_RETURN)
    {
      g_sh.unwind = UW_NONE;
      status = g_sh.last_status;
    }

  arena_release(f->arena);
  g_sh.func_depth--;
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
  bool had;
  unsigned flags;
};

/* Assignment words always contain '=' (the parser checked). */

static void split_assign(const char *text, char **name, const char **value)
{
  const char *eq = strchr(text, '=');

  if (eq == NULL)
    {
      *name = vs_xstrdup(text);
      *value = "";
      return;
    }

  *name = vs_xstrndup(text, (size_t)(eq - text));
  *value = eq + 1;
}

static int exec_assign_only(struct node_s *n)
{
  struct redir_saved_s sv;
  struct word_s *w;
  int status = 0;

  if (n->redirs != NULL && redir_apply(n->redirs, &sv, false) != 0)
    {
      return 1;
    }

  for (w = n->assigns; w != NULL && g_sh.unwind == UW_NONE; w = w->next)
    {
      char *name;
      const char *raw;
      char *val;

      split_assign(w->text, &name, &raw);
      val = expand_assign_str(raw);
      if (val == NULL || var_set(name, val) != 0)
        {
          status = 1;
          vs_special_error();
        }

      free(val);
      free(name);
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

  for (i = ntv - 1; i >= 0; i--)
    {
      struct var_s *v = var_lookup(tv[i].name);

      if (v != NULL)
        {
          if (keep)
            {
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
      free(tv[i].name);
    }

  free(tv);
}

static int exec_simple(struct node_s *n)
{
  struct fieldv_s argv;
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
  fv_init(&argv);

  if (expand_words(n->words, &argv) != 0)
    {
      fv_free(&argv);
      return g_sh.unwind == UW_EXIT ? g_sh.last_status : 1;
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
          status = 1;
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
      struct var_s *v;

      split_assign(w->text, &name, &raw);
      val = expand_assign_str(raw);
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
      status = call_builtin(b, argv.n, argv.v);
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
  char *subject = expand_word_str(n->words->text);
  struct case_item_s *item;
  int status = 0;

  if (subject == NULL)
    {
      return 1;
    }

  for (item = n->items; item != NULL; item = item->next)
    {
      struct word_s *w;
      bool matched = false;

      for (w = item->patterns; w != NULL && !matched; w = w->next)
        {
          struct pat_s pat = expand_pattern(w->text);

          matched = pat_match(pat.s, pat.q, pat.len, subject);
          pat_free(&pat);
        }

      if (matched)
        {
          status = exec_node(item->body);
          break;
        }
    }

  free(subject);
  return status;
}

static int exec_subshell(struct node_s *n)
{
  pid_t pid = vs_plat_fork();
  struct node_s *only = n->a;

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
      return 1;
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

      if (i == nst - 1)
        {
          status = s;
        }
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

      path = vs_plat_find_command(argv.v[0], var_get("PATH"), &err);
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

      if (i == nst - 1)
        {
          status = s;
        }
    }

  free(pipes);
  free(closes);
  free(pids);
  return status;
}

#endif

static int exec_pipeline(struct node_s *n)
{
  int nst = count_stages(n->a);
  int status;

  if (n->flag)
    {
      g_sh.noerrexit++;
    }

  status = (nst == 1) ? exec_node(n->a) : run_stages(n->a, nst);

  if (n->flag)
    {
      g_sh.noerrexit--;
      return status == 0 ? 1 : 0;
    }

  errexit_check(status);
  return status;
}

/* ---- Lists ------------------------------------------------------------------------------------- */

static int exec_async(struct node_s *n)
{
  pid_t pid = vs_plat_fork();

  if (pid < 0)
    {
      vs_err("&: background jobs are not supported on this platform yet");
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
          if (g_trap_pending)
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

int exec_node(struct node_s *n)
{
  int status = 0;

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

      if (node != NULL)
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

  return line.len > 0 ? sb_take(&line) : (sb_free(&line), NULL);
}

static char *file_next_line(void *ctx, bool continuation)
{
  (void)continuation;
  return read_stream_line((FILE *)ctx);
}

int run_file(const char *path)
{
  FILE *fp = fopen(path, "r");
  struct parser_s p;
  int status;

  if (fp == NULL)
    {
      vs_err("%s: %s", path, strerror(errno));
      return 127;
    }

  parser_init(&p, file_next_line, fp);
  status = run_source(&p, false);
  parser_free(&p);
  fclose(fp);
  return status;
}
