/*
 * rcfile.h -- startup files of an interactive shell (rcfile.c).
 */

#ifndef VAPORSHELL_RCFILE_H
#define VAPORSHELL_RCFILE_H

#include <stdbool.h>

/* Read the startup files, in order, each only if it exists:
 *
 *   login shells:  ~/.vsh_profile
 *   unless norc:   rcfile if given, else /etc/vaporshell/vsh.conf, then
 *                  $VS_RC or ~/.vaporshellrc
 *
 * ~ is $HOME; where that is unset (vaporOS today) /data stands in for it.
 * ~/.bashrc is never read implicitly: `source ~/.bashrc` from the rc file.
 */

/* Run a file as the shell's own startup code (an rc file, a completion spec): its
 * errors are reported but do not end the session. False if it is not a readable file.
 */

bool vs_rc_source(const char *path);

void vs_rc_load(bool login, bool norc, const char *rcfile);

#endif
