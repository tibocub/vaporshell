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
#include <stddef.h>
#include <sys/types.h>

/* The shell's state (vaporshell.h). Host: the one global; NuttX: an
 * instance per task, found by task id, because tasks share globals.
 */

struct shell_s *vs_plat_state_create(void);   /* zeroed, bound to this task */
void vs_plat_state_destroy(void);

/* NuttX's VFS resolves neither "." nor ".." in a path. Returns a path the
 * OS can open: 'path' itself when it is fine (always, on a host), else an
 * absolute, normalized copy in buf (n bytes). Use it for every filesystem
 * call the shell makes on a user-supplied path.
 */

#define VS_PATH_MAX 512

const char *vs_plat_fspath(const char *path, char *buf, size_t n);

/* Shorthand for a call site: stat(VS_FS(p), &st). The scratch buffer is a
 * compound literal, alive until the end of the enclosing block, which is
 * exactly as long as the call needs it.
 */

#define VS_FS(p) vs_plat_fspath((p), (char[VS_PATH_MAX]){ 0 }, VS_PATH_MAX)

/* Is stdin a terminal? (NuttX: assumed, as before.) */

bool vs_plat_interactive(void);

/* Identity, for bash's UID/EUID/HOSTNAME/OSTYPE. */

bool vs_plat_isatty(int fd);
long vs_plat_uid(void);
long vs_plat_euid(void);
int vs_plat_hostname(char *buf, size_t n);      /* 0 on success */
const char *vs_plat_ostype(void);

/* fork() where the OS has it, else -1 with errno ENOSYS. */

bool vs_plat_have_fork(void);
pid_t vs_plat_fork(void);

/* NuttX: variables assigned in the shell are also environment variables,
 * because a child shell is the only way it can run a substitution.
 */

bool vs_plat_export_all(void);

/* String ordering as bash does it for pathname sorting and [[ a < b ]]:
 * strcoll() in the collation locale on a host, plain byte order on NuttX (no
 * locales there). vs_plat_locale_update() re-reads LC_ALL / LC_COLLATE / LANG
 * from the shell's variables; it is called when one of them changes.
 */

int vs_plat_collate(const char *a, const char *b);
void vs_plat_locale_update(void);

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

/* Can a builtin of this name also be run as a separate program? A background
 * job (`cmd &`) needs a process, so it is started as that program (true,
 * false, pwd, test, ...); so is a pipeline stage where stages can neither
 * fork nor run in-process. With in-process pipelines (inproc.c) a pipeline
 * stage runs the shell's own builtin instead.
 */

bool vs_plat_external_fallback(const char *name);

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
