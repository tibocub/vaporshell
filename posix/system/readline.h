/*
 * Stand-in for NuttX's <system/readline.h> (standalone build only,
 * see posix/nuttx/config.h). Same contract as the NuttX readline()
 * vaporshell_main.c already calls: returns a malloc()'d line
 * *including* its trailing '\n' (tokenize.c depends on that), or
 * NULL at EOF; the caller frees it and prints the prompt itself.
 */

#ifndef VAPORSHELL_POSIX_READLINE_H
#define VAPORSHELL_POSIX_READLINE_H

char *readline(const char *prompt);

#endif
