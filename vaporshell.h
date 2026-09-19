/*
 * vaporshell.h -- what every module shares: small utilities, the
 * shell's global state, and the variable/function tables.
 *
 * Layering (each layer only calls downward):
 *
 *   vaporshell_main.c   entry point, interactive loop
 *   exec.c redir.c      run an AST
 *   builtins.c help.c   builtin commands
 *   expand.c arith.c    word expansion (glob.c: pattern matching)
 *   parser.c lexer.c    text -> AST (ast.h, parse.h)
 *   vars.c util.c       variable table, allocation, strings
 *   platform_*.c        the only code that differs between NuttX and a host OS
 */

#ifndef VAPORSHELL_H
#define VAPORSHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* Out-of-memory is fatal: the shell prints a message and exits. That keeps
 * every caller free of NULL checks, which is the right trade for a shell.
 */

void *vs_xmalloc(size_t n);
void *vs_xrealloc(void *p, size_t n);
char *vs_xstrdup(const char *s);
char *vs_xstrndup(const char *s, size_t n);

/* "vaporshell: <message>\n" on stderr. */

void vs_err(const char *fmt, ...);

/* Growable NUL-terminated string. */

struct sbuf_s
{
  char *s;
  size_t len;
  size_t cap;
};

void sb_init(struct sbuf_s *b);
void sb_addc(struct sbuf_s *b, char c);
void sb_addn(struct sbuf_s *b, const char *s, size_t n);
void sb_adds(struct sbuf_s *b, const char *s);
char *sb_take(struct sbuf_s *b);   /* malloc'd, buffer reset */
void sb_free(struct sbuf_s *b);

/* ---- Variables and functions (vars.c) ---------------------------------- */

#define VF_EXPORT   0x01
#define VF_READONLY 0x02

struct var_s
{
  struct var_s *next;
  char *name;
  char *value;            /* NULL: declared (export/readonly) but unset */
  unsigned flags;
};

struct node_s;
struct arena_s;

struct func_s
{
  struct func_s *next;
  char *name;
  struct node_s *body;
  struct arena_s *arena;  /* keeps 'body' alive; see arena_retain() */
};

bool is_valid_name(const char *s, size_t len);
struct var_s *var_lookup(const char *name);
const char *var_get(const char *name);           /* NULL if unset */
int var_set(const char *name, const char *value); /* -1: readonly */
int var_set_flags(const char *name, unsigned flags);
int var_unset(const char *name);                 /* -1: readonly */
char **var_build_env(void);                      /* free with env_free() */
void env_free(char **env);
void vars_import(char **environ_list);

struct func_s *func_find(const char *name);
void func_define(const char *name, struct node_s *body, struct arena_s *arena);
void func_unset(const char *name);

void pos_set(char **args, int n);                /* takes ownership of copies */
char **pos_swap(char **args, int n, int *old_n); /* returns previous list */
const char *pos_get(int i);                      /* 1-based; NULL if unset */

/* ---- Global shell state ------------------------------------------------ */

enum unwind_e
{
  UW_NONE = 0,
  UW_BREAK,
  UW_CONTINUE,
  UW_RETURN,
  UW_EXIT
};

struct shell_s
{
  bool opt_e;             /* set -e */
  bool opt_u;             /* set -u */
  bool opt_x;             /* set -x */
  bool opt_f;             /* set -f */
  bool opt_C;             /* set -C */
  bool interactive;
  int last_status;        /* $? */
  int cmdsub_status;      /* status of the last command substitution, or -1 */
  pid_t pid;              /* $$ (stays the parent's inside subshells) */
  pid_t last_bg;          /* $! */
  const char *arg0;       /* $0 */
  const char *self;       /* how to re-run this shell (platform_nuttx.c) */

  char **pos;             /* positional parameters, pos[0] is $1 */
  int npos;

  enum unwind_e unwind;   /* pending break/continue/return/exit */
  int unwind_count;       /* levels left for break/continue */
  int func_depth;
  int loop_depth;
  int noerrexit;          /* >0: inside a context where set -e is ignored */
  bool can_exec;          /* next simple command may replace this process */
  bool syntax_error;      /* set by run_source(); eval/. check it */

  struct var_s *vars;
  struct func_s *funcs;
};

extern struct shell_s g_sh;

/* Sets up g_sh from the process environment. */

void shell_init(const char *arg0);

#endif
