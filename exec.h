/*
 * exec.h -- running an AST, redirections, and the builtin table.
 */

#ifndef VAPORSHELL_EXEC_H
#define VAPORSHELL_EXEC_H

#include <stdbool.h>
#include <stdio.h>

#include "ast.h"
#include "mode.h"
#include "parse.h"

/* exec.c */

int exec_node(struct node_s *n);

/* Run every command the parser yields until end of input. If
 * 'recover', a syntax error is reported and reading continues (an
 * interactive session); otherwise it ends the run with status 2.
 */

int run_source(struct parser_s *p, bool recover);
int run_string(const char *text, size_t len);
int run_file(const char *path);

/* Run an already-expanded command. skip_functions: as "command name". */

int run_argv(int argc, char **argv, bool skip_functions);

/* How a name would be resolved, for command -v / type. */

enum cmd_kind_e { CK_NONE, CK_SPECIAL, CK_FUNCTION, CK_BUILTIN, CK_EXTERNAL };

enum cmd_kind_e classify_command(const char *name, char **path_out);

/* redir.c */

struct redir_saved_s
{
  struct
  {
    int fd;
    int saved;                /* -1: fd was closed before */
  } *e;
  int n;
};

int redir_apply(struct redir_s *list, struct redir_saved_s *sv, bool persist);
void redir_restore(struct redir_saved_s *sv);

/* builtins.c */

struct builtin_s
{
  const char *name;
  int (*fn)(int argc, char **argv);
  bool special;               /* POSIX special builtin */
  const char *help;
  unsigned modes;             /* VS_M_* profiles this builtin exists in */
};

extern const struct builtin_s g_vs_builtins[];     /* name == NULL terminated */
const struct builtin_s *builtin_find(const char *name);

/* A special builtin failed in a way POSIX makes fatal: end a non-interactive
 * shell if the profile says so (VF_SPECIAL_ERR_FATAL).
 */

void vs_special_error(void);

/* builtins.c: `set -o NAME` / `+o NAME`; returns -1 for an unknown name. */

int vs_set_named_option(const char *name, bool on);

/* help.c, test.c, traps.c */

int bi_help(int argc, char **argv);
int bi_test(int argc, char **argv);
/* Subshells without fork() -- inproc.c. */

bool vs_inproc_enabled(void);
int vs_inproc_subshell(struct node_s *body);
char *vs_inproc_cmdsub(const char *text, size_t len, int *status);
int vs_inproc_stage(struct node_s *stage, int in_fd, char **out, size_t *outlen);
void *vs_inproc_feed(int fd, char *buf, size_t len);   /* a handle for _wait, or NULL */
void vs_inproc_feed_wait(void *handle);
void trap_subshell_enter(void);
void trap_run_err(int status);
void trap_run_debug(void);
void trap_run_return(void);
void trap_subshell_leave(const struct shell_s *saved);

int vs_option_state(const char *name);

/* Backslash escapes (builtins_io.c): 's' points just after the backslash.
 * Appends what it denotes to 'out' and returns how many characters it used.
 */

#define ESC_STOP      0x1     /* \c ends the output */
#define ESC_OCT_PLAIN 0x2     /* \ddd as well as \0ddd */
#define ESC_HEXU      0x4     /* \xHH \uHHHH \UHHHHHHHH and \" \' \? */
#define ESC_E         0x8     /* \e and \E */
#define ESC_CTRL      0x10    /* \cX is a control character ($'...') */

size_t vs_esc_one(const char *s, unsigned flags, struct sbuf_s *out, bool *stop);
int bi_echo(int argc, char **argv);
int bi_getopts(int argc, char **argv);
int bi_local(int argc, char **argv);
int bi_hash(int argc, char **argv);
int bi_shopt(int argc, char **argv);

/* assign.c: assignment words. asg_is_word() only recognises them; */
bool asg_is_word(const char *text);

/* declare.c: the declaration builtins in bash mode */

int bi_declare(int argc, char **argv);
int bi_mapfile(int argc, char **argv);            /* mapfile.c: mapfile, readarray */
int bi_local_decl(int argc, char **argv);
int bi_export_decl(int argc, char **argv);
int bi_readonly_decl(int argc, char **argv);
void vs_print_array_body(const struct arr_s *a);
void vs_print_dq(const char *s);                     /* "..." or $'...' */      /* builtins.c: ([0]="x" ...) */
bool asg_ref_isset(const char *text);                /* -v name / name[i] / name[@] */
int asg_unset_ref(const char *text, bool *handled);  /* unset name[i]; handled=false if not that form */
size_t asg_subscript_end(const char *s, size_t len, size_t open);   /* the ] for a [ */
int assign_apply(const char *raw);
char *asg_target_name(const char *arg, bool *is_asg);   /* declaration builtins */
char *assign_decl_word(const char *raw, bool *is_raw);
int assign_apply_decl(const char *arg, bool is_raw, bool force_assoc);   /* declaration builtins */          /* name=v, name+=v, name[i]=v, name=(...) */
bool assign_scalar_parts(const char *raw, char **name, const char **value, bool *append);
int bi_pushd(int argc, char **argv);
int bi_popd(int argc, char **argv);
int bi_dirs(int argc, char **argv);
void dirstack_free(void);
void vs_shopt_defaults(void);
int bi_alias(int argc, char **argv);
int bi_unalias(int argc, char **argv);
#ifdef VAPORSHELL_POSIX
int bi_times(int argc, char **argv);
int bi_ulimit(int argc, char **argv);
#endif
int bi_printf(int argc, char **argv);
int bi_bracket(int argc, char **argv);
int bi_trap(int argc, char **argv);
int bi_kill(int argc, char **argv);

/* Run pending signal traps / the EXIT trap (traps.c). */

void trap_run_pending(void);
void trap_run_exit(void);
void trap_reset_in_child(void);

/* Read one line of arbitrary length from a stream (malloc'd, with '\n'). */

char *read_stream_line(FILE *fp);

#endif
