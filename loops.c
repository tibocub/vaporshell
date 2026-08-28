/*
 * loops.c -- for/while/until, sharing control.c's own keyword/depth-
 * aware scanning (control_internal.h) rather than reimplementing it.
 *
 * No break/continue yet (see the project's own README/roadmap) --
 * a loop always runs to completion based on its own condition/list,
 * the same way an "if" branch does, just repeatedly.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "control_internal.h"

int run_while_until(FAR char *construct_copy, FAR bool *should_exit,
                     bool is_while)
{
    struct marker_s markers[MAX_MARKERS];
    int nmarkers;
    FAR char *p;
    FAR char *cond_end;
    FAR char *body_start;
    FAR char *body_end;
    size_t condlen;
    size_t bodylen;
    FAR char *cond;
    FAR char *body;
    int status = 0;
    int mi = 0;

    nmarkers = find_markers(construct_copy, markers, MAX_MARKERS);
    p = skip_leading_keyword(construct_copy);

    while (mi < nmarkers && markers[mi].kind != MARKER_DO)
    {
        mi++;
    }

    if (mi >= nmarkers)
    {
        fprintf(stderr, "vaporshell: %s: missing 'do'\n",
                is_while ? "while" : "until");
        return 1;
    }

    cond_end = markers[mi].start;
    condlen = (size_t)(cond_end - p);

    body_start = markers[mi].after;
    body_end = (mi + 1 < nmarkers) ? markers[mi + 1].start
                                    : (construct_copy + strlen(construct_copy));
    bodylen = (size_t)(body_end - body_start);

    cond = malloc(condlen + 1);
    body = malloc(bodylen + 1);

    if (cond == NULL || body == NULL)
    {
        free(cond);
        free(body);
        return 1;
    }

    memcpy(cond, p, condlen);
    cond[condlen] = '\0';
    memcpy(body, body_start, bodylen);
    body[bodylen] = '\0';

    /* cond/body are re-copied fresh on every iteration below --
     * run_line() modifies its own input in place (same as tokenize()/
     * split_line() already do throughout this codebase), so reusing
     * the same buffer across iterations would corrupt it after the
     * first pass.
     */

    for (; ; )
    {
        FAR char *cond_copy = strdup(cond);
        FAR char *body_copy;
        int cond_status;

        if (cond_copy == NULL)
        {
            break;
        }

        cond_status = run_line(cond_copy, should_exit);
        free(cond_copy);

        if (*should_exit)
        {
            break;
        }

        if (is_while ? (cond_status != 0) : (cond_status == 0))
        {
            break;
        }

        body_copy = strdup(body);
        if (body_copy == NULL)
        {
            break;
        }

        status = run_line(body_copy, should_exit);
        free(body_copy);

        if (*should_exit)
        {
            break;
        }
    }

    free(cond);
    free(body);
    return status;
}

int run_for(FAR char *construct_copy, FAR bool *should_exit)
{
    struct marker_s markers[MAX_MARKERS];
    int nmarkers;
    FAR char *p;
    FAR char *name_start;
    size_t namelen;
    char name[64];
    FAR char *list_start;
    FAR char *list_end;
    FAR char *body_start;
    FAR char *body_end;
    size_t listlen;
    size_t bodylen;
    FAR char *list_text;
    FAR char *body;
    FAR char *raw_tokens[MAX_TOKENS];
    bool no_expand[MAX_TOKENS];
    FAR char *expanded[MAX_TOKENS];
    int ntok;
    int status = 0;
    int mi = 0;
    int i;

    nmarkers = find_markers(construct_copy, markers, MAX_MARKERS);
    p = skip_leading_keyword(construct_copy);

    while (*p == ' ' || *p == '\t')
    {
        p++;
    }

    name_start = p;
    while (is_ident_char(*p))
    {
        p++;
    }

    namelen = (size_t)(p - name_start);
    if (namelen == 0 || namelen >= sizeof(name))
    {
        fprintf(stderr, "vaporshell: for: invalid variable name\n");
        return 1;
    }

    memcpy(name, name_start, namelen);
    name[namelen] = '\0';

    while (mi < nmarkers && markers[mi].kind != MARKER_IN)
    {
        mi++;
    }

    if (mi >= nmarkers)
    {
        /* "for x; do ...; done" (no "in list", uses positional
         * parameters) is valid POSIX too, but positional parameters
         * ($1, $2, ...) aren't implemented yet -- a real, separate
         * gap (see the project's own README), not something to
         * silently guess at here.
         */

        fprintf(stderr, "vaporshell: for: missing 'in' (bare \"for x; do\" "
                        "needs positional parameters, not implemented yet)\n");
        return 1;
    }

    list_start = markers[mi].after;
    mi++;

    while (mi < nmarkers && markers[mi].kind != MARKER_DO)
    {
        mi++;
    }

    if (mi >= nmarkers)
    {
        fprintf(stderr, "vaporshell: for: missing 'do'\n");
        return 1;
    }

    list_end = markers[mi].start;
    listlen = (size_t)(list_end - list_start);

    body_start = markers[mi].after;
    body_end = (mi + 1 < nmarkers) ? markers[mi + 1].start
                                    : (construct_copy + strlen(construct_copy));
    bodylen = (size_t)(body_end - body_start);

    list_text = malloc(listlen + 1);
    body = malloc(bodylen + 1);

    if (list_text == NULL || body == NULL)
    {
        free(list_text);
        free(body);
        return 1;
    }

    memcpy(list_text, list_start, listlen);
    list_text[listlen] = '\0';
    memcpy(body, body_start, bodylen);
    body[bodylen] = '\0';

    /* The "do" marker's own .start is right at "do" itself, so
     * list_text as extracted above still has the one-liner form's
     * trailing ';' attached (e.g. "for x in a b c; do ..." -- the
     * text between "in" and "do" is " a b c;", ';' included).
     * tokenize() has no idea ';' is a statement separator at all --
     * only line.c's own split_line() treats it specially -- so
     * without stripping it here first, it just glues onto whatever
     * word precedes it ("c;") or becomes its own, spurious list item
     * (if a space separates it: "c ;"). Confirmed directly this is a
     * real bug, not a hypothetical: `for item in a b c; do echo
     * $item; done` was printing "c;" as one item instead of "c".
     * if/while/until's own condition text doesn't have this problem
     * -- it's run through run_line() before anything else touches
     * it, which already understands ';' correctly -- but a for
     * loop's item list is tokenized directly, never passing through
     * that same statement-splitting logic at all.
     */

    {
        FAR char *end = list_text + strlen(list_text);

        while (end > list_text &&
               (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        {
            end--;
        }

        if (end > list_text && end[-1] == ';')
        {
            end--;
        }

        *end = '\0';
    }

    /* Tokenize+expand the list into individual words -- the exact
     * same mechanism a command's own arguments go through (line.c's
     * expand_tokens()), since a for-loop's item list is really no
     * different: whitespace-separated words, quoting and $VAR
     * expansion both apply the same way.
     */

    ntok = tokenize(list_text, raw_tokens, no_expand, MAX_TOKENS);
    expand_tokens(raw_tokens, no_expand, ntok, expanded);

    for (i = 0; i < ntok; i++)
    {
        FAR char *body_copy;

        setenv(name, expanded[i], 1);

        body_copy = strdup(body);
        if (body_copy == NULL)
        {
            continue;
        }

        status = run_line(body_copy, should_exit);
        free(body_copy);

        if (*should_exit)
        {
            break;
        }
    }

    for (i = 0; i < ntok; i++)
    {
        free(expanded[i]);
    }

    free(list_text);
    free(body);
    return status;
}
