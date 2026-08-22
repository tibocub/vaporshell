/*
 * script.c -- reads and runs a file's worth of commands sequentially.
 * No control flow, no variable expansion yet (see docs/design.md's
 * own milestones) -- just the same tokenize()+run_command() pipeline
 * the interactive loop already uses, fed from a file instead of
 * readline().
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
 * interactively. Returns the last command's exit status (0 if the
 * file was empty or every line was blank/a comment), or 127 if the
 * file itself couldn't be opened, matching run_command()'s own
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
        FAR char *tokens[MAX_TOKENS];
        int ntok;

        ntok = tokenize(line, tokens, MAX_TOKENS);

        if (ntok > 0)
        {
            if (strcmp(tokens[0], "exit") == 0 ||
                strcmp(tokens[0], "quit") == 0)
            {
                break;
            }

            status = run_command(ntok, tokens);
        }
    }

    fclose(stream);
    return status;
}
