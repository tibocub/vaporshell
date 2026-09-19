/*
 * platform.h -- everything the shell needs from the OS that is not the
 * same on NuttX and on a host OS. Two implementations:
 *
 *   posix/platform.c    fork(), PATH search, posix_spawn (standalone build)
 *   platform_nuttx.c    posix_spawnp on the bare name, no fork
 *
 * The rest of the shell is written against this interface only.
 */

#ifndef VAPORSHELL_PLATFORM_H
#define VAPORSHELL_PLATFORM_H

#include <stdbool.h>
#include <sys/types.h>

/* Is stdin a terminal? (NuttX: assumed, as before.) */

bool vs_plat_interactive(void);

/* fork() where the OS has it, else -1 with errno ENOSYS. */

bool vs_plat_have_fork(void);
pid_t vs_plat_fork(void);

/* NuttX: variables assigned in the shell are also environment variables,
 * because a child shell is the only way it can run a substitution.
 */

bool vs_plat_export_all(void);

/* Locate 'name' for execution. Returns a malloc()'d path, or NULL with
 * *err = ENOENT (not found) or EACCES (found, not executable). NuttX
 * resolves builtin apps by bare name itself, so it returns the name.
 */

char *vs_plat_find_command(const char *name, const char *path_var, int *err);

/* Start a program. in_fd/out_fd >= 0 become its stdin/stdout; every fd in
 * close_fds is closed in the child. Returns 0 or an errno value.
 */

int vs_plat_spawn(const char *path, char *const argv[], char *const envp[],
                  int in_fd, int out_fd, const int *close_fds, int nclose,
                  pid_t *pid);

/* Replace this process (host only). Returns only on failure. */

int vs_plat_exec(const char *path, char *const argv[], char *const envp[]);

/* dup() into a descriptor >= 10 (not inherited by children where the OS
 * allows it), so saved descriptors don't collide with a script's own.
 */

int vs_plat_dup_high(int fd);

/* Run "vaporshell -c text" with stdout captured; NuttX only (the host
 * build forks instead). Returns malloc()'d output, trailing newlines kept.
 */

char *vs_plat_capture_via_self(const char *text, int *status);

#endif
