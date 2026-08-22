/*
 * tokenize.c -- whitespace + basic quote tokenizing, no variable
 * expansion, no command substitution, no control flow. Those need
 * either a real grammar (mrsh, still an open spike per docs/design.md)
 * or meaningfully more hand-written parsing -- not bundled into this
 * same, deliberately minimal pass.
 */

#include <nuttx/config.h>

#include "vaporshell.h"

/****************************************************************************
 * Splits 'line' into tokens in place -- argv[] entries point directly
 * into 'line's own buffer, so 'line' must stay alive (and gets freed
 * by the caller, once) for as long as argv[] is used. Whitespace-
 * separated, with single/double quotes treated as one token each
 * (quote characters themselves stripped, nothing expanded inside
 * them -- deliberately not real POSIX quoting yet, see this file's
 * own top-of-file comment). Returns argc; argv[argc] is always NULL.
 ****************************************************************************/

int tokenize(FAR char *line, FAR char *argv[], int max_tokens)
{
    int argc = 0;
    FAR char *p = line;

    /* readline()'s own doc comment claims "the final newline removed,
     * so only the text of the line remains" -- confirmed directly
     * against readline_fd.c that this is not actually true in the
     * code we're calling: readline_fd()'s own doc comment says "if a
     * newline is read, it is stored into the buffer," and the
     * public readline() wrapper does zero post-processing of what
     * readline_fd() returns before handing it back. Every line here
     * really does end in '\n'. Treating '\n' (and defensively '\r',
     * given this project's whole CR/LF history with vterm_fb) as a
     * delimiter, same as space/tab, handles this at the one place it
     * actually matters rather than requiring a separate strip-the-
     * newline preprocessing step that's easy to forget to call from
     * every caller (the interactive loop, -c mode, and script files
     * all use this same function).
     */

    for (; ; )
    {
        char quote = '\0';
        FAR char *start;

        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        {
            p++;
        }

        if (*p == '\0')
        {
            break;
        }

        /* '#' starting a new token (not inside quotes, handled below --
         * this check runs before the quote check, so a quoted '#' is
         * never reached here) means everything from here to the end
         * of the line is a comment: "echo hi # comment" and a
         * full-line "# comment" both need this, and scripts use
         * comments constantly -- this was missing entirely before,
         * silently treating '#' as a literal argument character.
         */

        if (*p == '#')
        {
            break;
        }

        if (*p == '\'' || *p == '"')
        {
            quote = *p;
            p++;
        }

        start = p;

        if (quote != '\0')
        {
            while (*p != '\0' && *p != quote)
            {
                p++;
            }
        }
        else
        {
            while (*p != '\0' && *p != ' ' && *p != '\t' &&
                   *p != '\n' && *p != '\r')
            {
                p++;
            }
        }

        if (argc >= max_tokens - 1)
        {
            break;
        }

        argv[argc++] = start;

        if (*p != '\0')
        {
            *p = '\0';
            p++;
        }
    }

    argv[argc] = NULL;
    return argc;
}
