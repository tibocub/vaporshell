/*
 * script.c -- reads and runs a file's worth of commands sequentially,
 * one line at a time, via run_line() (line.c) -- the same
 * split/expand/assign/exit-detect pipeline the interactive loop uses,
 * except for a line starting an if/then/elif/else/fi construct
 * (control.c), which gets handed off there instead, along with a
 * callback (script_next_line, below) letting it keep reading further
 * lines straight from this same file until "fi" closes it.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "vaporshell.h"
#include "control.h"

/****************************************************************************
 * 'continuation' is ignored -- a script has no prompt to switch at
 * all, unlike the interactive case (vaporshell_main.c's own
 * next_line_fn). Returns a newly malloc()'d copy of the next line, or
 * NULL at EOF -- control.c owns and frees whatever this returns.
 ****************************************************************************/

static FAR char *script_next_line(FAR void *ctx, bool continuation)
{
    FAR FILE *stream = (FAR FILE *)ctx;
    char buf[MAX_LINE];

    (void)continuation;

    if (fgets(buf, sizeof(buf), stream) == NULL)
    {
        return NULL;
    }

    return strdup(buf);
}

/****************************************************************************
 * No prompt, no history: this isn't interactive use, so none of
 * readline()'s own interactive machinery is invoked at all. "exit"/
 * "quit" stop the script early, same meaning as they already have
 * interactively (run_line()'s/run_control_construct()'s own
 * should_exit out-parameter is what signals this here). Returns the
 * last command's exit status (0 if the file was empty or every line
 * was blank/a comment), or 127 if the file itself couldn't be opened,
 * matching run_command()'s own "command not found" convention for a
 * consistent meaning across both failure modes.
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

        if (is_control_start(line))
        {
            FAR char *first_line = strdup(line);

            if (first_line == NULL)
            {
                continue;
            }

            status = run_control_construct(first_line, script_next_line,
                                            stream, &should_exit);
        }
        else
        {
            status = run_line(line, &should_exit);
        }

        if (should_exit)
        {
            break;
        }
    }

    fclose(stream);
    return status;
}
