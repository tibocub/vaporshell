/*
 * line.c -- splits a line on unquoted ';', '&&', '||' into segments
 * and runs each in order, applying the conditional-execution
 * semantics &&/|| are supposed to have (run the next segment only if
 * the previous one succeeded/failed, respectively -- ';' always runs
 * the next one regardless of the previous status). This is what
 * main.c's interactive loop and script.c both call now, instead of
 * tokenize()+run_command() directly -- variable assignment/expansion
 * and exit/quit detection belong here too, at the same "one whole
 * line" level, not duplicated in every caller. A segment containing
 * an unquoted '|' (pipeline.c) is handed off there instead of being
 * tokenized/run directly here -- see that file's own comment.
 *
 * No command substitution, no control flow (if/for/while) yet --
 * those need real parsing, not more cases bolted onto this same,
 * deliberately simple splitter.
 */

#include <nuttx/config.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "expand.h"
#include "control.h"

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

        if (*p == ';' || *p == '\n' ||
            (*p == '&' && p[1] == '&') ||
            (*p == '|' && p[1] == '|'))
        {
            /* '\n' is a statement separator with the same "always
             * runs regardless of the previous status" meaning as
             * ';' -- required for control.c's own multi-line if/then/
             * else bodies (and, later, for/while) to work at all:
             * without this, a body like "echo a\necho b\n" would be
             * handed to run_line() as one single string and tokenize
             * into one command ("echo a echo b"), since tokenize()
             * already treats '\n' as ordinary whitespace, not a
             * statement boundary -- that distinction has to happen
             * here, before tokenizing, not there. Doesn't change
             * single-line behavior at all: script.c/vaporshell_main.c
             * already hand this function one line at a time, each
             * with at most one trailing '\n', which just becomes an
             * empty final segment tokenize() already skips.
             */

            char this_sep = (*p == ';' || *p == '\n') ? ';' : *p;
            int width = (*p == ';' || *p == '\n') ? 1 : 2;

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

/****************************************************************************
 * Expands an already-tokenized argv (tokenize()'s own raw_tokens/
 * no_expand output) into a newly-malloc()'d argv_out -- caller must
 * free() each entry 0..ntok-1 once done. Shared by this file's own
 * "real command" case below and by pipeline.c (each pipeline stage
 * needs this same tokenize()-then-expand step, just without the exit/
 * assignment checks this file's own run_line() does first).
 ****************************************************************************/

void expand_tokens(FAR char *raw_tokens[], FAR bool no_expand[], int ntok,
                    FAR char *argv_out[])
{
    int j;

    for (j = 0; j < ntok; j++)
    {
        argv_out[j] = expand_token(raw_tokens[j], no_expand[j]);
        if (argv_out[j] == NULL)
        {
            /* Allocation failure expanding this one token -- fall
             * back to the raw, unexpanded text rather than pass a
             * NULL into argv_out, which posix_spawnp would choke on.
             * Not a case worth stopping an otherwise-runnable command
             * over.
             */

            argv_out[j] = strdup(raw_tokens[j]);
        }
    }

    argv_out[ntok] = NULL;
}

/* g_last_status is updated here, per segment, not just once by the
 * caller after this whole function returns -- $? (expand.c) needs to
 * see the immediately-preceding command's status even mid-line, e.g.
 * "false; echo $?" has to see false's status, not whatever the
 * *previous line* left behind, which is what would happen if only
 * main.c's/script.c's own "g_last_status = run_line(...)" assignment
 * ever touched it.
 */

/* Used only for a nested if/then/else/fi found *within* an already-
 * extracted body/cond text (control.c's own build_branches() already
 * guarantees that text is complete and balanced before it's ever
 * handed to run_line()) -- accumulate_construct()'s own while loop
 * should never actually need another line here, so this should never
 * really be called; it exists so a genuinely malformed nested
 * construct fails with control.c's own "unexpected end of input"
 * error instead of calling through a NULL function pointer.
 */

static FAR char *no_more_lines(FAR void *ctx, bool continuation)
{
    (void)ctx;
    (void)continuation;
    return NULL;
}

/****************************************************************************
 * Rebuilds a contiguous string from segments[start_idx..nsegs-1],
 * reinserting each segment's own original separator (';', '&&', or
 * '||') between them -- needed when a *later* segment turns out to
 * start a control construct (e.g. "x=start; while ...; done"):
 * split_line() has already destroyed the construct's own internal
 * ';' structure by the time this is discovered, so the only way to
 * hand run_control_construct() a text it can correctly re-parse (its
 * own "do"/"done" could be several naively-split segments away) is to
 * undo that splitting for everything from here onward.
 ****************************************************************************/

static FAR char *rejoin_segments(struct segment_s segments[], int start_idx,
                                  int nsegs)
{
    size_t total = 0;
    int i;
    FAR char *result;
    FAR char *p;

    for (i = start_idx; i < nsegs; i++)
    {
        total += strlen(segments[i].text);
        if (i > start_idx)
        {
            total += (segments[i].sep == ';') ? 1 : 2;
        }
    }

    result = malloc(total + 1);
    if (result == NULL)
    {
        return NULL;
    }

    p = result;
    for (i = start_idx; i < nsegs; i++)
    {
        size_t len;

        if (i > start_idx)
        {
            if (segments[i].sep == ';')
            {
                *p++ = ';';
            }
            else
            {
                *p++ = segments[i].sep;
                *p++ = segments[i].sep;
            }
        }

        len = strlen(segments[i].text);
        memcpy(p, segments[i].text, len);
        p += len;
    }

    *p = '\0';
    return result;
}

int run_line(FAR char *line, FAR bool *should_exit)
{
    struct segment_s segments[MAX_SEGMENTS];
    int nsegs;
    int status = 0;
    int i;

    *should_exit = false;

    /* Checked here, before split_line() ever runs, not just once at
     * the top level (main.c/script.c) -- confirmed directly this
     * matters: a *nested* if, inside another if's own body, reaches
     * this function too (control.c's own run_branches() calls
     * run_line() on each branch's body text), and split_line() itself
     * has no idea what an "if" even is -- without this check first,
     * it would just split a nested "if ... then ... fi" on its own
     * embedded newlines/';' the same as any other text, scattering
     * "if"/"then"/"fi" into separate, unrelated segments and running
     * each as a literal (nonexistent) command name. The same check,
     * per segment rather than once up front, is what makes a
     * construct starting *later* in the line (e.g. "x=start; while
     * ...; done") work too -- see the per-segment check inside the
     * loop below, and rejoin_segments()'s own comment for why a
     * naively-split segment alone isn't enough to hand off correctly.
     */

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

        if (has_unquoted_pipe(segments[i].text))
        {
            status = run_pipeline(segments[i].text);
            g_last_status = status;
            continue;
        }

        if (is_control_start(segments[i].text))
        {
            FAR char *reconstructed = rejoin_segments(segments, i, nsegs);

            if (reconstructed == NULL)
            {
                return 1;
            }

            /* run_control_construct() already handles everything from
             * here onward, including any remainder after the
             * construct's own close -- returning directly rather than
             * continuing this loop, which would otherwise re-process
             * the very segments already folded into 'reconstructed'.
             */

            status = run_control_construct(reconstructed, no_more_lines,
                                            NULL, should_exit);
            g_last_status = status;
            return status;
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
            g_last_status = status;
            continue;
        }

        {
            FAR char *expanded[MAX_TOKENS];
            int j;

            expand_tokens(raw_tokens, no_expand, ntok, expanded);

            status = run_command(ntok, expanded);
            g_last_status = status;

            for (j = 0; j < ntok; j++)
            {
                free(expanded[j]);
            }
        }
    }

    return status;
}
