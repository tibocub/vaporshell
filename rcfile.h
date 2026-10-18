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

void vs_rc_load(bool login, bool norc, const char *rcfile);

#endif
