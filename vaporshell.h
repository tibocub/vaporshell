/*
 * vaporshell.h -- shared across every .c file in this app. Kept
 * small and flat on purpose (one header, not one per .c file) --
 * this is still a small enough codebase that per-file headers would
 * be more ceremony than benefit; revisit that if/when any one area
 * (expansion, control flow) grows enough to want its own, narrower
 * header instead of reaching into this shared one.
 */

#ifndef VAPORSHELL_H
#define VAPORSHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <nuttx/compiler.h>
#include <spawn.h>

#define MAX_TOKENS 64
#define MAX_PWD    128
#define MAX_LINE   1024

/* main.c */
extern int g_last_status;

/* What subst.c spawns for command substitution: "vaporshell" (resolved
 * through $PATH) by default, an absolute path on the standalone build
 * (see posix/self_path.c).
 */

extern FAR const char *g_self_exe;

#ifdef VAPORSHELL_POSIX
/* posix/self_path.c */
FAR char *vs_resolve_self(FAR const char *argv0);
#endif

/* tokenize.c */
int tokenize(FAR char *line, FAR char *argv[], FAR bool no_expand[],
             int max_tokens);

/* line.c */
int run_line(FAR char *line, FAR bool *should_exit);
void expand_tokens(FAR char *raw_tokens[], FAR bool no_expand[], int ntok,
                    FAR char *argv_out[]);

/* pipeline.c */
bool has_unquoted_pipe(FAR const char *text);
int run_pipeline(FAR char *text);

/* dispatch.c */
bool is_tbx_command(FAR const char *name);
extern const char *const g_tbx_commands[];

/* help.c */
void run_help(void);

/* script.c */
int run_script_file(FAR const char *path);

/* builtins.c */
int run_builtin(int argc, FAR char *argv[], FAR bool *handled);

/* exec.c */
int run_command(int argc, FAR char *argv[]);
int spawn_command(int argc, FAR char *argv[],
                   FAR posix_spawn_file_actions_t *actions, FAR pid_t *pid);

#endif
