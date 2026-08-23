/*
 * tokenize.c -- whitespace + quote tokenizing (plus tracking which
 * tokens were purely single-quoted, for expand.c's benefit). Also
 * groups $(...) and `...` as single, whitespace-preserving regions
 * (like quotes, but content isn't stripped -- expand.c/subst.c do the
 * actual substitution once a token's real boundaries are already
 * settled here) -- confirmed directly this is required, not optional:
 * without it, `echo before \`echo mid\` after` and
 * `echo $(echo nested $(echo deep))` both silently split into several
 * separate tokens the moment a space appears inside the substitution,
 * breaking it before expand.c ever sees a coherent construct to
 * recognize.
 */

#include <nuttx/config.h>

#include "vaporshell.h"

/****************************************************************************
 * Splits 'line' into tokens in place -- argv[] entries point directly
 * into 'line's own buffer, so 'line' must stay alive (and gets freed
 * by the caller, once) for as long as argv[] is used. Whitespace-
 * separated, but quotes and $(...)/`...` can start and end *anywhere*
 * within a token, not just at its very start -- confirmed directly
 * this matters for quotes (`name="tibo"` and `name="tibo smith"` both
 * need it) and separately for command substitution (see this file's
 * own top-of-file comment). Quote characters themselves are stripped
 * as tokens are collected (via a read/write pointer pair scanning the
 * same buffer -- no separate allocation needed); $(...)/`...`
 * delimiters are *not* stripped here (unlike quotes) -- expand.c's
 * own find_command_subst() (subst.c) needs to see them intact to
 * recognize and run the substitution afterward. Returns argc;
 * argv[argc] is always NULL.
 *
 * no_expand[i] is set to true iff argv[i] consists *entirely* of
 * single-quoted content (single quotes suppress $VAR expansion and
 * command substitution alike, double quotes suppress neither) -- a
 * token mixing single-quoted and unquoted/double-quoted/substitution
 * parts (e.g. `$x'lit'`) is treated as expandable as a whole, a
 * known, deliberate simplification rather than tracking expansion per
 * sub-segment within one token. Pass NULL if the caller doesn't need
 * this -- nothing currently does; line.c is the only real caller, and
 * always wants it.
 ****************************************************************************/

int tokenize(FAR char *line, FAR char *argv[], FAR bool no_expand[],
             int max_tokens)
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
        FAR char *start;
        FAR char *dst;
        char quote = '\0';
        int paren_depth = 0;
        bool in_backtick = false;
        bool saw_quote = false;
        bool saw_unquoted_or_double = false;

        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        {
            p++;
        }

        if (*p == '\0')
        {
            break;
        }

        /* '#' starting a new token (checked here, before any
         * in-token quote/substitution state exists, so a quoted '#'
         * later inside a token is never reached by this check) means
         * everything from here to the end of the line is a comment:
         * "echo hi # comment" and a full-line "# comment" both need
         * this, and scripts use comments constantly.
         */

        if (*p == '#')
        {
            break;
        }

        /* dst trails p, only falling behind when a quote character
         * itself is skipped rather than copied -- the standard
         * in-place compaction technique, still no separate
         * allocation, just two pointers into the same buffer instead
         * of one.
         */

        start = p;
        dst = p;

        while (*p != '\0')
        {
            if (quote != '\0')
            {
                if (*p == quote)
                {
                    quote = '\0';
                    p++;
                    continue;
                }

                *dst++ = *p++;
                continue;
            }

            if (paren_depth > 0)
            {
                /* Not tracking quotes *within* $(...) here (e.g.
                 * $(echo ")") -- a known, narrower edge case than the
                 * one this whole block exists to fix; whitespace and
                 * every character copy through unchanged either way
                 * until the matching close.
                 */

                if (*p == '(')
                {
                    paren_depth++;
                }
                else if (*p == ')')
                {
                    paren_depth--;
                }

                *dst++ = *p++;
                continue;
            }

            if (in_backtick)
            {
                if (*p == '`')
                {
                    in_backtick = false;
                }

                *dst++ = *p++;
                continue;
            }

            if (*p == '$' && p[1] == '(')
            {
                paren_depth = 1;
                saw_unquoted_or_double = true;
                *dst++ = *p++;
                *dst++ = *p++;
                continue;
            }

            if (*p == '`')
            {
                in_backtick = true;
                saw_unquoted_or_double = true;
                *dst++ = *p++;
                continue;
            }

            if (*p == '\'' || *p == '"')
            {
                quote = *p;
                saw_quote = true;
                if (quote == '"')
                {
                    saw_unquoted_or_double = true;
                }

                p++;
                continue;
            }

            if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            {
                break;
            }

            saw_unquoted_or_double = true;
            *dst++ = *p++;
        }

        if (argc >= max_tokens - 1)
        {
            break;
        }

        if (no_expand != NULL)
        {
            no_expand[argc] = saw_quote && !saw_unquoted_or_double;
        }

        argv[argc++] = start;

        if (*p != '\0')
        {
            p++;
        }

        *dst = '\0';
    }

    argv[argc] = NULL;
    return argc;
}
