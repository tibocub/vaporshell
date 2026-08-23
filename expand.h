#ifndef VAPORSHELL_EXPAND_H
#define VAPORSHELL_EXPAND_H

#include "vaporshell.h"

/* Detects "NAME=VALUE" (NAME a valid identifier: starts with a letter
 * or underscore, continues with letters/digits/underscores, followed
 * immediately by '=', no space before it -- "variable = x" is a
 * command named "variable" with arguments "=" "x", not an assignment,
 * same distinction real shells make and learnxinyminutes.com/bash
 * calls out explicitly). On a match, sets *name and *value to
 * newly-malloc()'d strings (caller frees both) and returns true.
 *
 * Deliberately narrow for now: only recognizes a *whole command line*
 * that's just one assignment ("x=1"), not "x=1 command args" (a
 * temporary, command-scoped variable, real shells' own more advanced
 * case) -- that's a real, known gap, not an oversight.
 */

bool is_assignment(FAR const char *token, FAR char **name, FAR char **value);

/* Expands $NAME and ${NAME} references in 'token' against the current
 * environment (getenv()) -- unset variables expand to empty, same as
 * real shells. Returns a newly-malloc()'d string (caller frees) --
 * never modifies 'token' in place, since the expanded result can be a
 * different length than the original. 'in_single_quotes' skips
 * expansion entirely and just returns a copy, matching real shells:
 * single quotes suppress all expansion.
 */

FAR char *expand_token(FAR const char *token, bool in_single_quotes);

#endif
