/*
 * posix/readline.c -- minimal line input for the standalone build:
 * no editing, no history, no completion -- just enough that the
 * interactive loop and control.c's continuation prompt work. Real
 * line editing is deliberately a separate piece of work, and this
 * file (behind the one readline() call site in vaporshell_main.c) is
 * the seam it plugs into.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "system/readline.h"

char *readline(const char *prompt)
{
    char *line = NULL;
    size_t cap = 0;

    (void)prompt; /* already written by the caller, see readline.h */

    if (getline(&line, &cap, stdin) < 0)
    {
        free(line);
        return NULL;
    }

    return line;
}
