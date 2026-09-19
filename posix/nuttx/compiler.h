/*
 * Stand-in for <nuttx/compiler.h> (standalone build only, see
 * posix/nuttx/config.h). FAR is NuttX's near/far pointer qualifier,
 * empty on every flat-address-space target, which includes every
 * POSIX host. environ is declared here because POSIX only requires
 * unistd.h to declare it under _GNU_SOURCE, and vaporshell builds
 * with strict -std=c99 -D_POSIX_C_SOURCE for portability.
 */

#ifndef VAPORSHELL_POSIX_COMPILER_H
#define VAPORSHELL_POSIX_COMPILER_H

#define FAR

extern char **environ;

#endif
