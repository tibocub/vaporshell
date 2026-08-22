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

    /* posix_spawnp, not posix_spawn: PATH resolution
     * (CONFIG_LIBC_ENVPATH) is what makes a bare command name work at
     * all instead of requiring a full path. Its own error convention
     * is unusual and easy to get backwards -- it returns 0 on success
     * or a positive errno value directly on failure, NOT -1 with
     * errno set the way most POSIX calls work.
     */

    ret = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);

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
            return 1;
        }

        tbx_argv[0] = "tbx";
        for (i = 0; i < argc; i++)
        {
            tbx_argv[i + 1] = argv[i];
        }

        tbx_argv[argc + 1] = NULL;

        ret = posix_spawnp(&pid, "tbx", NULL, NULL, tbx_argv, environ);
    }

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
