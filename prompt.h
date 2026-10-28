/*
 * prompt.h -- PS1/PS2/PS4 expansion (prompt.c).
 */

#ifndef VAPORSHELL_PROMPT_H
#define VAPORSHELL_PROMPT_H

#include <stddef.h>

/* What \[ and \] become, so the editor can tell which bytes take no room. */

#define PROMPT_IGN_START '\001'
#define PROMPT_IGN_END   '\002'

/* Backslash escapes only (\u \h \w ...). malloc()'d. */

char *vash_prompt_decode(const char *ps);

/* Escapes, then parameter / command / arithmetic expansion of the result.
 * malloc()'d; \[ \] are still marked.
 */

char *vash_prompt_expand(const char *ps);

/* Remove the \[ \] markers in place. */

void vash_prompt_strip(char *s);

#endif
