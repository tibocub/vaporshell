/*
 * ledit.h -- the line editor (ledit.c): emacs-style editing, history,
 * tab completion. The same code runs on Linux and on vaporOS; the terminal
 * is reached only through platform.h (vs_plat_tty_*).
 */

#ifndef VAPORSHELL_LEDIT_H
#define VAPORSHELL_LEDIT_H

#include <stdbool.h>

/* Can the editor be used (standard input is a terminal it can drive)? */

bool vs_ledit_usable(void);

/* Read one line. 'prompt' is already expanded, with \[ \] marked
 * (prompt.h). Returns a malloc()'d line including its newline, or NULL at
 * end of input (Ctrl-D on an empty line). Ctrl-C returns "\n". Lines are
 * added to the history unless 'continuation' (a PS2 prompt).
 */

char *vs_ledit_read(const char *prompt, bool continuation);

#endif
