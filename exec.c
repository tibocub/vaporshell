/*
 * exec.c -- resolves and runs a single command: builtin first, then
 * PATH resolution + posix_spawnp for anything else.
 *
 * A real limitation worth being upfront about, confirmed by checking
 * the actual generated apps/builtin/builtin_list.h from a real build:
 * NSH's own commands (ls, cat, pwd, cd, ...) are NOT separately
 * spawnable programs -- they're internal to nshlib's own command
 * table, not entries in the builtin-apps table posix_spawn resolves
 * against. Only separately-registered apps/external programs, plus
 * anything reachable via the tbx fallback below, actually run through
 * this.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <errno.h>

#include "vaporshell.h"

/****************************************************************************
 * Spawns argv[0] via PATH resolution, falling back to the tbx
 * multicall table on ENOENT -- shared by run_command() (actions ==
 * NULL, inherit stdin/stdout/stderr as-is, the normal case) and
 * pipeline.c (a real file_actions redirecting stdin/stdout into a
 * pipe). Deliberately does *not* check builtins at all -- that's the
 * caller's job (run_command() does it; pipeline.c deliberately
 * doesn't, see its own top-of-file comment on why builtins aren't
 * supported as pipeline stages yet) -- this function always spawns a
 * real process.
 *
 * On success, sets *pid and returns 0. On failure, returns the errno
 * value directly -- posix_spawnp's own convention, not the usual
 * "-1 and errno set" -- and *pid is left unset.
 ****************************************************************************/

int spawn_command(int argc, FAR char *argv[],
                   FAR posix_spawn_file_actions_t *actions, FAR pid_t *pid)
{
    int ret;

    ret = posix_spawnp(pid, argv[0], actions, NULL, argv, environ);

    /* Fall back to the tbx multicall table only on ENOENT (command
     * not found via normal PATH resolution) -- a real installed
     * program with the same name should always win, the same
     * precedence a real Unix shell gives $PATH over any builtin
     * utility replacement. ENOENT confirmed directly as the real
     * error this path returns: it's what "ls: No such file or
     * directory" (strerror(ENOENT)) came from before this fallback
     * existed.
     */

    if (ret == ENOENT && is_tbx_command(argv[0]))
    {
        FAR char *tbx_argv[MAX_TOKENS + 1];
        int i;

        /* tokenize()'s own bound already guarantees argc <=
         * MAX_TOKENS - 1, but check the actual array capacity here
         * rather than assume that bound can never change independently.
         */

        if (argc + 2 > (int)(sizeof(tbx_argv) / sizeof(tbx_argv[0])))
        {
            fprintf(stderr, "vaporshell: %s: too many arguments\n", argv[0]);
            return E2BIG;
        }

        tbx_argv[0] = "tbx";
        for (i = 0; i < argc; i++)
        {
            tbx_argv[i + 1] = argv[i];
        }

        tbx_argv[argc + 1] = NULL;

        ret = posix_spawnp(pid, "tbx", actions, NULL, tbx_argv, environ);
    }

    return ret;
}

/****************************************************************************
 * Returns the exit status (or 127, the standard shell convention for
 * "command not found", if spawning failed outright).
 ****************************************************************************/

int run_command(int argc, FAR char *argv[])
{
    bool handled;
    int status;
    pid_t pid;
    int ret;

    if (argc == 0)
    {
        return 0;
    }

    status = run_builtin(argc, argv, &handled);
    if (handled)
    {
        return status;
    }

    ret = spawn_command(argc, argv, NULL, &pid);
    if (ret != 0)
    {
        fprintf(stderr, "vaporshell: %s: %s\n", argv[0], strerror(ret));
        return 127;
    }

    ret = waitpid(pid, &status, 0);
    if (ret < 0)
    {
        fprintf(stderr, "vaporshell: waitpid: %s\n", strerror(errno));
        return 1;
    }

    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }

    return 1;
}
