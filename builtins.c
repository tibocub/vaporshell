/*
 * builtins.c -- builtins have to live here regardless of how good
 * NuttX's spawn story is: cd mutates *this* process's cwd, and
 * ./source read commands meant to affect *this* shell session --
 * neither could ever be a spawned program, since a child process's
 * own cwd/env changes can never propagate back up to its parent.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "vaporshell.h"

/****************************************************************************
 * Sets *handled so the caller knows whether to fall through to
 * posix_spawnp.
 ****************************************************************************/

int run_builtin(int argc, FAR char *argv[], FAR bool *handled)
{
    *handled = true;

    if (strcmp(argv[0], "cd") == 0)
    {
        char cwd[MAX_PWD];

        if (argc < 2)
        {
            /* No $HOME concept sorted out yet (see docs/design.md) --
             * requiring an argument rather than guessing is safer
             * than a wrong default.
             */

            fprintf(stderr, "cd: missing argument\n");
            return 1;
        }

        if (chdir(argv[1]) != 0)
        {
            fprintf(stderr, "cd: %s: %s\n", argv[1], strerror(errno));
            return 1;
        }

        if (getcwd(cwd, sizeof(cwd)) != NULL)
        {
            setenv("PWD", cwd, 1);
        }

        return 0;
    }

    if (strcmp(argv[0], "help") == 0)
    {
        run_help();
        return 0;
    }

    if (strcmp(argv[0], ".") == 0 || strcmp(argv[0], "source") == 0)
    {
        /* Has to be a builtin, not a spawned program, for the same
         * reason cd is: a sourced script's cd/variable changes need
         * to affect *this* shell session, which a child process's
         * own env/cwd changes could never propagate back up.
         */

        if (argc < 2)
        {
            fprintf(stderr, "%s: missing filename argument\n", argv[0]);
            return 1;
        }

        return run_script_file(argv[1]);
    }

    *handled = false;
    return 0;
}
