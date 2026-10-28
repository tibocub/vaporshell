/*
 * compfish.h -- fish-style `complete` specifications (compfish.c).
 */

#ifndef VAPORSHELL_COMPFISH_H
#define VAPORSHELL_COMPFISH_H

#include <stdbool.h>

#include "complete.h"

/* `complete` with fish flags (-c CMD -s X -l NAME -d TEXT -a WORDS -n COND -f ...).
 * Returns -1 if the arguments are not fish-style, so bash's `complete` handles
 * them; else the status of the call.
 */

int vash_complete_fish(int argc, char **argv);

/* Candidates for 'word' from the fish-style specs of 'cmd' (the VASH_SPEC_* bits). */

int vash_comp_fish(const char *cmd, const char *line, int point, const char *word,
                 vash_comp_list_f sink, void *ctx);

/* Has 'cmd' any fish-style spec, or was its completion file already run? */

bool vash_fish_known(const char *cmd);
void vash_fish_mark(const char *cmd);

/* compgen.c: COMP_WORDS & co. for a condition or a -F function. */

void vash_set_comp_vars(const char *line, int point);

void vash_fish_free(void);

#endif
