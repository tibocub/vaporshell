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

/* `bind` (bind.c): add or remove a key binding, find a command by its readline
 * name, and walk the bindings (user ones first, then the built-in table).
 * 'seq' is the exact bytes the key sends. cmd 0 is no command; the shell
 * command number for `bind -x` is vs_ledit_cmd_lookup("shell-command").
 */

int vs_ledit_bind(const char *seq, int len, int cmd, const char *shell);
int vs_ledit_unbind(const char *seq, int len);
int vs_ledit_cmd_lookup(const char *name);
const char *vs_ledit_cmd_name(int i);       /* NULL: none by that number */
int vs_ledit_ncmds(void);
bool vs_ledit_binding(int idx, const char **seq, int *len, const char **name, const char **shell);

#endif
