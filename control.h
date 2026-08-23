#ifndef VAPORSHELL_CONTROL_H
#define VAPORSHELL_CONTROL_H

#include "vaporshell.h"

/* Reads one more line of input -- ctx is opaque to control.c, owned
 * by whichever caller supplies it (script.c's own FILE*, or nothing
 * at all for the interactive case). Returns a newly malloc()'d string
 * (control.c frees it), or NULL on EOF/error. 'continuation' is true
 * for every call after the first for a given construct -- lets an
 * interactive implementation switch to a "> " secondary prompt, the
 * way a real shell does while still waiting on more input.
 */

typedef FAR char *(*next_line_fn)(FAR void *ctx, bool continuation);

/* True iff 'line' starts a control construct this file knows how to
 * handle -- currently just "if" (leading whitespace skipped, checked
 * as a whole word, not e.g. matching "iffy"). Checked by
 * script.c/vaporshell_main.c before treating a line as an ordinary
 * command to hand to run_line() directly.
 */

bool is_control_start(FAR const char *line);

/* Returns a pointer just past the matching "fi" that closes the if
 * construct 'text' starts with, or NULL if 'text' doesn't contain a
 * complete, balanced one. Used by line.c to separate a construct from
 * whatever text follows its closing "fi" in the same string -- see
 * this function's own definition (control.c) for the full reasoning.
 */

FAR char *find_construct_end(FAR char *text);

/* Reads (via read_line) as many further lines as needed to complete
 * the construct 'first_line' started, then parses and runs the whole
 * thing -- works identically whether the construct was all on one
 * line (';'-separated) or spans many (newline-separated, the common
 * script style); line.c's own split_line() treats both the same way.
 * Takes ownership of first_line (frees it, even on failure). Returns
 * the construct's own exit status.
 */

int run_control_construct(FAR char *first_line, next_line_fn read_line,
                           FAR void *ctx, FAR bool *should_exit);

#endif
