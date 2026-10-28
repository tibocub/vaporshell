/*
 * history.h -- command history (history.c): the in-memory list the line
 * editor walks, HISTFILE persistence, and the `history` builtin.
 */

#ifndef VAPORSHELL_HISTORY_H
#define VAPORSHELL_HISTORY_H

/* Load HISTFILE (default ~/.vash_history) if the shell is interactive. */

void vash_hist_init(void);

/* Record a command (no trailing newline), honouring HISTCONTROL / HISTSIZE. */

void vash_hist_add(const char *line);

int vash_hist_count(void);
const char *vash_hist_get(int i);            /* 0 is the oldest */

/* The line with its `!` events expanded (malloc'd), NULL if it has none, "" if an event is unknown. */

char *vash_hist_expand(const char *line);

/* Append the entries not yet saved to HISTFILE. */

void vash_hist_save(void);

/* Free the list. */

void vash_hist_fini(void);

#endif
