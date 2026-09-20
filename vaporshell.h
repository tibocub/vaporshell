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
#include <time.h>
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

/* Lexical path normalization: see util.c. */

int vs_path_normalize(const char *cwd, const char *path, char *out, size_t n);

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

/* An indexed array: only the elements that are set, sorted by index (array.c). */

struct arr_elem_s
{
  long idx;
  char *val;
};

struct arr_s
{
  struct arr_elem_s *e;
  size_t n;
  size_t cap;
};

struct var_s
{
  struct var_s *next;
  char *name;
  char *value;            /* NULL: declared (export/readonly) but unset */
  unsigned flags;
  struct arr_s *arr;      /* non-NULL: an array. 'value' is then unused, and
                           * $name means element 0, as in bash */
};

/* `local`: a variable's outer state, put back when its function returns. */

struct local_s
{
  struct local_s *next;
  char *name;
  char *old;                  /* outer value; only meaningful if had */
  bool had;                   /* the variable had a value */
  struct arr_s *old_arr;      /* the outer variable was an array: it is kept here */
  unsigned flags;
  int depth;                  /* function nesting level that declared it */
};

struct alias_s
{
  struct alias_s *next;
  char *name;
  char *value;
};

struct hash_s
{
  struct hash_s *next;
  char *name;
  char *path;
  int hits;
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
int var_set(const char *name, const char *value);   /* -1: readonly */
bool var_is_locale_var(const char *name);          /* LC_ALL, LC_COLLATE, LANG */
int var_set_flags(const char *name, unsigned flags);
int var_unset(const char *name);                 /* -1: readonly */

/* Arrays (array.c, and vars.c for the variable-level calls). Element indexes
 * are already resolved: negative subscripts are handled by the expander.
 */

struct arr_s *arr_new(void);
void arr_free(struct arr_s *a);
void arr_clear(struct arr_s *a);
struct arr_s *arr_clone(const struct arr_s *a);
size_t arr_find(const struct arr_s *a, long idx, bool *found);
const char *arr_get(const struct arr_s *a, long idx);
void arr_set(struct arr_s *a, long idx, const char *val);
void arr_unset(struct arr_s *a, long idx);
void arr_append(struct arr_s *a, const char *val);
long arr_max_index(const struct arr_s *a);

struct arr_s *var_array(const char *name, bool create);   /* NULL: not an array */
bool var_is_array(const char *name);
const char *var_elem_get(const char *name, long idx);      /* NULL: unset */
int var_elem_set(const char *name, long idx, const char *val);   /* -1: readonly */
int var_elem_unset(const char *name, long idx);                  /* -1: readonly */
int var_array_replace(const char *name, struct arr_s *arr);       /* takes 'arr'; -1: readonly */
char **var_build_env(void);                      /* free with env_free() */
void env_free(char **env);
void vars_import(char **environ_list);

/* `local` support: declare a variable local to the running function, and
 * restore everything declared at 'depth' when that function returns.
 */

int var_local_declare(const char *name, const char *value, bool inherit);
void var_locals_pop(int depth);

/* Aliases (alias.c) and the command hash (hash.c). */

const struct alias_s *alias_find(const char *name);
void aliases_free(void);
char *hash_find_command(const char *name, const char *path_var, int *err);
void hash_clear(void);

struct func_s *func_find(const char *name);
void func_define(const char *name, struct node_s *body, struct arena_s *arena);
void func_unset(const char *name);

void pos_set(char **args, int n);                /* takes ownership of copies */
char **pos_swap(char **args, int n, int *old_n); /* returns previous list */
const char *pos_get(int i);                      /* 1-based; NULL if unset */

/* ---- Shell state ---------------------------------------------------------
 *
 * Everything mutable lives in one struct shell_s so that two shells can
 * coexist. That matters on NuttX: a flat build gives every instance of an
 * app the *same* global data, so a second vaporshell (a `vaporshell -c`
 * child, or another terminal) would otherwise overwrite the first one's
 * variables, functions and options. On a host OS there is one shell per
 * process and g_sh is a plain global; on NuttX g_sh is the instance
 * registered for the calling task (platform_nuttx.c).
 */

#define VS_NTRAPS 68
#define VS_TRAP_DEBUG  65             /* bash pseudo-signals, after the real ones, */
#define VS_TRAP_ERR    66             /* in the order bash lists them */
#define VS_TRAP_RETURN 67

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
  bool opt_a;             /* set -a: export every assigned variable */
  bool opt_n;             /* set -n: read commands but do not run them */
  bool opt_v;             /* set -v: echo input lines as they are read */
  bool opt_pipefail;      /* set -o pipefail (bash) */
  int trap_depth;         /* inside a trap action */
  int in_subshell;        /* running in-process as a subshell (inproc.c) */
  bool force_inproc;      /* VS_INPROC: use in-process subshells even with fork() */
  int dot_depth;          /* nesting of `.`/source: `return` is valid inside */
  time_t seconds_base;    /* $SECONDS counts from here */
  unsigned rand_state;    /* $RANDOM */
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

  unsigned long features;     /* mode.h: the active profile's feature bits */
  int profile;                /* enum vs_profile_e */

  /* shopt options that change behaviour (shopt.c); the rest are only
   * remembered so `shopt` can report them.
   */

  bool so_nullglob;
  bool so_dotglob;
  bool so_failglob;
  bool so_nocaseglob;
  bool so_nocasematch;
  bool so_extglob;
  bool so_globstar;
  bool so_expand_aliases;
  bool so_patsub;
  unsigned char so_generic[64];

  char **dirstack;            /* pushd/popd: saved directories, most recent first */
  int ndirs;
  char *trap_parent[VS_NTRAPS];   /* bash: a subshell's `trap` lists these (the parent's) */
  bool trap_dirty;                /* ...until the subshell sets a trap of its own */
  int return_fdepth;              /* function depth a RETURN trap was set at (0: top level) */
  bool in_err_trap;           /* an ERR/DEBUG/RETURN action is running */
  bool in_debug_trap;
  bool in_return_trap;

  int lineno;                 /* line of the command being run: $LINENO */
  struct local_s *locals;
  struct alias_s *aliases;
  struct hash_s *hash;
  int getopts_pos;            /* getopts: index inside a clustered option arg */
  char getopts_last[24];      /* OPTIND as getopts last stored it */

  char *trap_action[VS_NTRAPS];        /* traps.c: NULL default, "" ignore */
  volatile int trap_flag[VS_NTRAPS];   /* set by signal handlers */
  volatile int trap_pending;
};

#ifdef VAPORSHELL_POSIX
extern struct shell_s g_vs_state;
#  define g_sh g_vs_state
#else
struct shell_s *vs_state(void);
#  define g_sh (*vs_state())
#endif

/* Creates this task's shell state from the process environment. */

void shell_init(const char *arg0);

/* Frees everything shell_init() and the shell's own run allocated. */

void shell_fini(void);

#endif
