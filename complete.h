/*
 * complete.h -- tab completion candidates (complete.c).
 */

#ifndef VAPORSHELL_COMPLETE_H
#define VAPORSHELL_COMPLETE_H

typedef void (*vs_comp_list_f)(void *ctx, const char *name);

/* Complete the word at the end of line[0..len). The text to insert goes to
 * 'ins' (the common part of all candidates); every candidate is passed to
 * list(). Returns the number of candidates.
 */

int vs_complete(const char *line, int len, char *ins, int inslen,
                vs_comp_list_f list, void *ctx);

#endif
