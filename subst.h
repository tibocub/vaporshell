#ifndef VAPORSHELL_SUBST_H
#define VAPORSHELL_SUBST_H

#include "vaporshell.h"

/* Runs 'cmd_text' as a full command line (its own semicolons,
 * variables, &&/||, everything run_line() already handles) and
 * captures its stdout, with trailing newlines stripped -- the same
 * behavior real command substitution ($(...) and `...`) has. Returns
 * a newly-malloc()'d string (caller frees), never NULL -- an empty
 * string on any failure (couldn't spawn, couldn't allocate, ...)
 * rather than propagating an error into what's fundamentally still a
 * string-expansion step.
 */

FAR char *capture_command_output(FAR const char *cmd_text);

/* If 'p' points at the start of a command substitution ($( or ` ),
 * finds its full extent and sets cmd_start and cmd_end to the command
 * text's own bounds (exclusive of the $(/` and )/` delimiters) and
 * *after to where scanning should resume once it's been substituted.
 * Returns false if 'p' doesn't start a command substitution at all,
 * or if one starts but is unterminated (caller's own choice for that
 * case: don't guess at a truncated command, just fall through to
 * treating '$'/'`' as literal, same as expand_token() already does
 * for a lone '$' not followed by a valid name).
 */

bool find_command_subst(FAR const char *p, FAR const char **cmd_start,
                         FAR const char **cmd_end, FAR const char **after);

#endif
