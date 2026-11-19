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

#if defined(VASH_NO_FISH) || defined(VASH_NO_COMPLETION)
/* No fish-style specs in this build (config/Kconfig). */
static inline int vash_complete_fish(int argc, char **argv) { (void)argc; (void)argv; return -1; }
static inline int vash_comp_fish(const char *cmd, const char *line, int point, const char *word,
                                 vash_comp_list_f sink, void *ctx)
{ (void)cmd; (void)line; (void)point; (void)word; (void)sink; (void)ctx; return 0; }
static inline bool vash_fish_known(const char *cmd) { (void)cmd; return false; }
static inline void vash_fish_mark(const char *cmd) { (void)cmd; }
static inline void vash_fish_free(void) {}
#else
int vash_complete_fish(int argc, char **argv);

/* Candidates for 'word' from the fish-style specs of 'cmd' (the VASH_SPEC_* bits). */

int vash_comp_fish(const char *cmd, const char *line, int point, const char *word,
                 vash_comp_list_f sink, void *ctx);

/* Has 'cmd' any fish-style spec, or was its completion file already run? */

bool vash_fish_known(const char *cmd);
void vash_fish_mark(const char *cmd);
void vash_fish_free(void);
#endif

/* compgen.c: COMP_WORDS & co. for a condition or a -F function. */

void vash_set_comp_vars(const char *line, int point);

#endif
