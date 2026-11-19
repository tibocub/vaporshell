/*
 * ledit.h -- the line editor (ledit.c): emacs-style editing, history,
 * tab completion. The same code runs on Linux and on vaporOS; the terminal
 * is reached only through platform.h (vash_plat_tty_*).
 */

#ifndef VAPORSHELL_LEDIT_H
#define VAPORSHELL_LEDIT_H

#include <stdbool.h>
#include <stddef.h>

/* Can the editor be used (standard input is a terminal it can drive)? */

#ifdef VASH_NO_EDITOR
/* No line editor in this build (config/Kconfig): the shell reads plain lines. */
static inline bool vash_ledit_usable(void) { return false; }
static inline char *vash_ledit_read(const char *prompt, bool continuation)
{ (void)prompt; (void)continuation; return NULL; }
#else
bool vash_ledit_usable(void);

/* Read one line. 'prompt' is already expanded, with \[ \] marked
 * (prompt.h). Returns a malloc()'d line including its newline, or NULL at
 * end of input (Ctrl-D on an empty line). Ctrl-C returns "\n". Lines are
 * added to the history unless 'continuation' (a PS2 prompt).
 */

char *vash_ledit_read(const char *prompt, bool continuation);

/* `bind` (bind.c): add or remove a key binding, find a command by its readline
 * name, and walk the bindings (user ones first, then the built-in table).
 * 'seq' is the exact bytes the key sends. cmd 0 is no command; the shell
 * command number for `bind -x` is vash_ledit_cmd_lookup("shell-command").
 */

int vash_ledit_bind(const char *seq, int len, int cmd, const char *shell);
int vash_ledit_unbind(const char *seq, int len);
int vash_ledit_cmd_lookup(const char *name);
const char *vash_ledit_cmd_name(int i);       /* NULL: none by that number */
int vash_ledit_ncmds(void);
bool vash_ledit_binding(int idx, const char **seq, int *len, const char **name, const char **shell);

#endif /* VASH_NO_EDITOR */

/* Set the terminal window title (xterm OSC 2; control bytes dropped, cut at 100
 * bytes). Nothing is written on terminals that have no title (linux, vt*) or with
 * VASH_TITLE=0. The shell shows the running command's line, and user@host:dir at
 * each prompt (before PROMPT_COMMAND, so one that sets its own title wins).
 */

#if defined(VASH_NO_EDITOR) || defined(VASH_NO_TITLE)
static inline void vash_ledit_title(const char *t) { (void)t; }
#else
void vash_ledit_title(const char *t);
#endif

#endif
