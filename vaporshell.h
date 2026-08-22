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

#define MAX_TOKENS 64
#define MAX_PWD    128
#define MAX_LINE   1024

/* main.c */
extern int g_last_status;

/* tokenize.c */
int tokenize(FAR char *line, FAR char *argv[], int max_tokens);

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

#endif
