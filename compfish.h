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

int vs_complete_fish(int argc, char **argv);

/* Candidates for 'word' from the fish-style specs of 'cmd' (the VS_SPEC_* bits). */

int vs_comp_fish(const char *cmd, const char *line, int point, const char *word,
                 vs_comp_list_f sink, void *ctx);

/* Has 'cmd' any fish-style spec, or was its completion file already run? */

bool vs_fish_known(const char *cmd);
void vs_fish_mark(const char *cmd);

/* compgen.c: COMP_WORDS & co. for a condition or a -F function. */

void vs_set_comp_vars(const char *line, int point);

void vs_fish_free(void);

#endif
