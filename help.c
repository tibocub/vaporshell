/*
 * help.c -- prints every command vaporshell can actually run, in
 * three groups: vaporshell's own builtins (hardcoded, there are only
 * a few), the commands run via tbx (from g_tbx_commands, the same
 * table run_command() itself checks -- toybox- and NSH-sourced alike,
 * see dispatch.c's own comment), and everything else spawnable from
 * /bin (read via opendir()/readdir() -- confirmed safe against a
 * NAMED, absolute path here, unlike the bare "." case dirtree.c's own
 * NuttX fixes were about; "ls /bin" already works correctly through
 * that same distinction). This third group is what actually answers
 * "is X a real program" for things like vhello/vlua/vi/vcat/tbx
 * itself -- information g_tbx_commands doesn't have.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#include "vaporshell.h"

void run_help(void)
{
#ifndef VAPORSHELL_POSIX
    int i;
    FAR DIR *dir;
    FAR struct dirent *entry;
#endif

    printf("vaporshell builtins:\n");
    printf("  cd exit quit help . source\n");

#ifndef VAPORSHELL_POSIX
    /* tbx and /bin only mean something on vaporOS -- on a host OS,
     * everything else is just whatever $PATH resolves.
     */

    printf("\ncommands (via tbx):\n");
    printf(" ");
    for (i = 0; g_tbx_commands[i] != NULL; i++)
    {
        printf(" %s", g_tbx_commands[i]);
    }

    printf("\n\nother programs (/bin):\n");
    printf(" ");

    dir = opendir("/bin");
    if (dir == NULL)
    {
        printf(" (couldn't read /bin: %s)", strerror(errno));
    }
    else
    {
        while ((entry = readdir(dir)) != NULL)
        {
            /* binfs has no "." or ".." entries of its own (confirmed
             * directly, same as the pseudo-fs behavior dirtree.c had
             * to work around for real directories) -- this check is
             * defensive, not covering a known gap here.
             */

            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }

            printf(" %s", entry->d_name);
        }

        closedir(dir);
    }

#endif

    printf("\n");
}
