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
int bi_bracket(int argc, char **argv);
int bi_trap(int argc, char **argv);
int bi_kill(int argc, char **argv);

/* Run pending signal traps / the EXIT trap (traps.c). */

void trap_run_pending(void);
void trap_run_exit(void);
void trap_reset_in_child(void);
extern volatile int g_trap_pending;

/* Read one line of arbitrary length from a stream (malloc'd, with '\n'). */

char *read_stream_line(FILE *fp);

#endif
