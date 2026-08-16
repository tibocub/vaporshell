/*
 * docs/vaporshell.md, Milestone 1, step 1: the smallest possible
 * thing that proves two things at once, before any real shell logic
 * exists --
 *
 *   1. vaporshell can be registered and run as its own program (not
 *      NSH itself) via the same apps/external Makefile/Kconfig/*_main.c
 *      shape every other vaporOS app already uses.
 *   2. apps/system/readline's readline() -- the same one NSH itself
 *      uses, already fought through in detail getting vterm_fb's echo
 *      working -- is reusable from a completely different caller with
 *      no changes needed. This is the substrate the rest of
 *      vaporshell is going to be built on, so it's worth proving on
 *      its own, isolated from parsing/spawning/pipes/anything else
 *      that could also go wrong at the same time and make it unclear
 *      which piece actually broke.
 *
 * Deliberately does nothing beyond that yet: no tokenizing, no PATH
 * resolution, no posix_spawn. Just read a line, print it back,
 * "exit"/"quit" to leave. Runs as an ordinary NSH builtin for now --
 * `nsh> vaporshell` from a plain sim:nsh build -- not wired in as any
 * board's boot entrypoint. Keeping it decoupled from vterm_fb's
 * graphics/pty machinery on purpose, the same way libvterm's own
 * first milestone (vtermtest) was proven standalone before anything
 * about the framebuffer or NSH wiring was involved -- so the actual
 * shell logic can be iterated on fast, against the simplest possible
 * target, before it ever has to also work through a pty.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system/readline.h"

int main(int argc, char *argv[])
{
    for (; ; )
    {
        FAR char *line = readline("vaporshell$ ");

        if (line == NULL)
        {
            /* NULL means EOF (Ctrl+D) -- readline()'s own documented
             * behavior, not an error case to special-case around.
             */

            printf("\n");
            break;
        }

        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0)
        {
            free(line);
            break;
        }

        if (strlen(line) > 0)
        {
            printf("%s\n", line);
        }

        free(line);
    }

    return 0;
}
