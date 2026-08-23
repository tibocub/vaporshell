/*
 * line.c -- splits a line on unquoted ';', '&&', '||' into segments
 * and runs each in order, applying the conditional-execution
 * semantics &&/|| are supposed to have (run the next segment only if
 * the previous one succeeded/failed, respectively -- ';' always runs
 * the next one regardless of the previous status). This is what
 * main.c's interactive loop and script.c both call now, instead of
 * tokenize()+run_command() directly -- variable assignment/expansion
 * and exit/quit detection belong here too, at the same "one whole
 * line" level, not duplicated in every caller.
 *
 * No pipes ('|', as opposed to '||') yet, no command substitution, no
 * control flow (if/for/while) -- those need real parsing, not more
 * cases bolted onto this same, deliberately simple splitter.
 */

#include <nuttx/config.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "expand.h"

struct segment_s
{
    FAR char *text;
    char sep; /* separator BEFORE this segment: ';', '&' (for &&),
               * '|' (for ||), or '\0' for the first segment */
};

#define MAX_SEGMENTS 32

/****************************************************************************
 * Splits 'line' (destructively, like tokenize() -- segments[].text
 * point into 'line's own buffer, not separately allocated) on
 * unquoted ';', '&&', '||'.
 ****************************************************************************/

static int split_line(FAR char *line, struct segment_s segments[],
                       int max_segments)
{
    int count = 0;
    FAR char *p = line;
    FAR char *start = line;
    char quote = '\0';
    char pending_sep = '\0';

    while (*p != '\0')
    {
        if (quote != '\0')
        {
            if (*p == quote)
            {
                quote = '\0';
            }

            p++;
            continue;
        }

        if (*p == '\'' || *p == '"')
        {
            quote = *p;
            p++;
            continue;
        }

        if (*p == ';' ||
            (*p == '&' && p[1] == '&') ||
            (*p == '|' && p[1] == '|'))
        {
            char this_sep = *p;
            int width = (*p == ';') ? 1 : 2;

            if (count < max_segments - 1)
            {
                *p = '\0';
                segments[count].text = start;
                segments[count].sep = pending_sep;
                count++;
            }

            p += width;
            start = p;
            pending_sep = this_sep;
            continue;
        }

        p++;
    }

    if (count < max_segments - 1)
    {
        segments[count].text = start;
        segments[count].sep = pending_sep;
        count++;
    }

    return count;
}

/****************************************************************************
 * Should this segment run at all, given how the previous one exited?
 * ';' (or the first segment, sep == '\0') always runs. '&&' only if
 * the previous segment succeeded (status 0). '||' only if it failed.
 ****************************************************************************/

static bool should_run(char sep, int prev_status)
{
    if (sep == '&')
    {
        return prev_status == 0;
    }

    if (sep == '|')
    {
        return prev_status != 0;
    }

    return true;
}

int run_line(FAR char *line, FAR bool *should_exit)
{
    struct segment_s segments[MAX_SEGMENTS];
    int nsegs;
    int status = 0;
    int i;

    *should_exit = false;

    nsegs = split_line(line, segments, MAX_SEGMENTS);

    for (i = 0; i < nsegs; i++)
    {
        FAR char *raw_tokens[MAX_TOKENS];
        bool no_expand[MAX_TOKENS];
        FAR char *name;
        FAR char *value;
        int ntok;

        if (!should_run(segments[i].sep, status))
        {
            continue;
        }

        ntok = tokenize(segments[i].text, raw_tokens, no_expand, MAX_TOKENS);

        if (ntok == 0)
        {
            continue;
        }

        if (strcmp(raw_tokens[0], "exit") == 0 ||
            strcmp(raw_tokens[0], "quit") == 0)
        {
            *should_exit = true;
            return status;
        }

        /* A whole segment that's just "NAME=value" is an assignment,
         * not a command -- deliberately not handling "NAME=value cmd
         * args" (a temporary, command-scoped variable) yet, see
         * expand.h's own note on why that's a real, separate gap.
         */

        if (ntok == 1 && is_assignment(raw_tokens[0], &name, &value))
        {
            FAR char *expanded_value = expand_token(value, false);

            if (expanded_value != NULL)
            {
                setenv(name, expanded_value, 1);
                free(expanded_value);
            }

            free(name);
            free(value);
            status = 0;
            continue;
        }

        {
            FAR char *expanded[MAX_TOKENS];
            int j;

            for (j = 0; j < ntok; j++)
            {
                expanded[j] = expand_token(raw_tokens[j], no_expand[j]);
                if (expanded[j] == NULL)
                {
                    /* Allocation failure expanding this one token --
                     * fall back to the raw, unexpanded text rather
                     * than pass a NULL into argv[], which posix_spawnp
                     * would choke on. Not a case worth stopping the
                     * whole line over.
                     */

                    expanded[j] = strdup(raw_tokens[j]);
                }
            }

            expanded[ntok] = NULL;

            status = run_command(ntok, expanded);

            for (j = 0; j < ntok; j++)
            {
                free(expanded[j]);
            }
        }
    }

    return status;
}
