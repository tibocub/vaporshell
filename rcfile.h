/*
 * rcfile.h -- startup files of an interactive shell (rcfile.c).
 */

#ifndef VAPORSHELL_RCFILE_H
#define VAPORSHELL_RCFILE_H

#include <stdbool.h>

/* Read the startup files, in order, each only if it exists (none with norc):
 *
 *   /etc/vaporshell/vash.conf, ~/.inputrc,
 *   rcfile if given, else $VASH_RC, and nothing more; otherwise
 *   ~/.vashrc (vaporshell-only settings; may set LOAD_BASH_FILES=0), then
 *   unless LOAD_BASH_FILES=0, the bash files:
 *     login shell:  /etc/profile, then the first of ~/.bash_profile,
 *                   ~/.bash_login, ~/.profile
 *     otherwise:    ~/.bashrc
 *
 * ~ is $HOME; where that is unset (vaporOS today) /data stands in for it.
 */

/* Run a file as the shell's own startup code (an rc file, a completion spec): its
 * errors are reported but do not end the session. False if it is not a readable file.
 */

bool vash_rc_source(const char *path);

void vash_rc_load(bool login, bool norc, const char *rcfile);

#endif
