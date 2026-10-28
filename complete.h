/*
 * complete.h -- tab completion candidates (complete.c).
 */

#ifndef VAPORSHELL_COMPLETE_H
#define VAPORSHELL_COMPLETE_H

typedef void (*vash_comp_list_f)(void *ctx, const char *name, const char *desc);   /* desc may be NULL */

/* A candidate of vash_complete(): the callback KEEPS name and desc (malloc'd; desc may
 * be NULL) and frees them. What completes the word with it is
 * vash_comp_tail(name, suffix, skip) with the *skip vash_complete() returned.
 */

typedef void (*vash_comp_cand_f)(void *ctx, char *name, char *desc, char suffix);

char *vash_comp_tail(const char *name, char suffix, size_t skip);

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
#include <stddef.h>

/* Candidates for the last word of line[0..point) from the specs of command 'cmd'
 * (see compgen.c, compfish.c). Returns 0 if the command has none, else
 * VASH_SPEC_FOUND, plus VASH_SPEC_EXCLUSIVE if files should not be offered as well.
 */

#define VASH_SPEC_FOUND     1
#define VASH_SPEC_EXCLUSIVE 2

int vash_comp_from_spec(const char *cmd, const char *line, int point, const char *word,
                       vash_comp_list_f sink, void *ctx);

/* vash_is_command() looks in the $PATH table as it was at the last vash_commands_refresh()
 * (one stat per directory), so a caller checking several words refreshes once.
 */

void vash_commands_refresh(void);
bool vash_is_command(const char *name);
bool vash_comp_filenames(void);   /* complete -o filenames / compopt -o filenames: candidates are file names */
bool vash_comp_nospace(void);   /* compopt -o nospace in the completion function that just ran (compgen.c) */
void vash_each_user(void (*fn)(const char *name, void *arg), void *arg);   /* compgen.c */
void vash_each_path_command(void (*fn)(const char *name, void *arg), void *arg);

int vash_complete(const char *line, int len, char *ins, int inslen, int *del,
                size_t *skip, vash_comp_cand_f list, void *ctx);

#endif
