/*
 * complete.h -- tab completion candidates (complete.c).
 */

#ifndef VAPORSHELL_COMPLETE_H
#define VAPORSHELL_COMPLETE_H

typedef void (*vs_comp_list_f)(void *ctx, const char *name, const char *desc);   /* desc may be NULL */

/* A candidate of vs_complete(): its name, and the text that completes the word
 * with it (what to insert at the cursor).
 */

typedef void (*vs_comp_cand_f)(void *ctx, const char *name, const char *tail, const char *desc);

/* Complete the word at the end of line[0..len). The text to insert goes to
 * 'ins' (the common part of all candidates); every candidate is passed to
 * list() with the text that would complete it. Returns the number of candidates.
 *
 * If nothing starts with the word, names that match it more loosely are used (the
 * same letters in another case, then names that contain it). They replace what
 * was typed: *del is how many bytes before the cursor to remove first, and 'ins' and
 * the candidates' text are then the whole replacement. *del is 0 for an ordinary match.
 */

#include <stdbool.h>

/* Candidates for the last word of line[0..point) from the specs of command 'cmd'
 * (see compgen.c, compfish.c). Returns 0 if the command has none, else
 * VS_SPEC_FOUND, plus VS_SPEC_EXCLUSIVE if files should not be offered as well.
 */

#define VS_SPEC_FOUND     1
#define VS_SPEC_EXCLUSIVE 2

int vs_comp_from_spec(const char *cmd, const char *line, int point, const char *word,
                       vs_comp_list_f sink, void *ctx);

/* vs_is_command() looks in the $PATH table as it was at the last vs_commands_refresh()
 * (one stat per directory), so a caller checking several words refreshes once.
 */

void vs_commands_refresh(void);
bool vs_is_command(const char *name);
void vs_each_path_command(void (*fn)(const char *name, void *arg), void *arg);

int vs_complete(const char *line, int len, char *ins, int inslen, int *del,
                vs_comp_cand_f list, void *ctx);

#endif
