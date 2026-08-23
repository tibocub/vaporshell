/*
 * script.c -- reads and runs a file's worth of commands sequentially,
 * one line at a time, via run_line() (line.c) -- the same
 * split/expand/assign/exit-detect pipeline the interactive loop uses.
 * No control flow (if/for/while) yet -- see docs/design.md's own
 * milestones for what's next.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "vaporshell.h"

/****************************************************************************
 * No prompt, no history: this isn't interactive use, so none of
 * readline()'s own interactive machinery is invoked at all. "exit"/
 * "quit" stop the script early, same meaning as they already have
 * interactively (run_line()'s own should_exit out-parameter is what
 * signals this here). Returns the last command's exit status (0 if
 * the file was empty or every line was blank/a comment), or 127 if
 * the file itself couldn't be opened, matching run_command()'s own
 * "command not found" convention for a consistent meaning across
 * both failure modes.
 ****************************************************************************/

int run_script_file(FAR const char *path)
{
    FAR FILE *stream;
    char line[MAX_LINE];
    int status = 0;

    stream = fopen(path, "r");
    if (stream == NULL)
    {
        fprintf(stderr, "vaporshell: %s: %s\n", path, strerror(errno));
        return 127;
    }

    while (fgets(line, sizeof(line), stream) != NULL)
    {
        bool should_exit;

        status = run_line(line, &should_exit);

        if (should_exit)
        {
            break;
        }
    }

    fclose(stream);
    return status;
}
