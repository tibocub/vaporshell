/*
 * control.c -- if/then/elif/else/fi: the first real control-flow
 * construct. Works identically whether it's all on one line
 * (';'-separated) or spans multiple lines (the common script style)
 * -- both are handled the same way here, since line.c's own
 * split_line() now treats '\n' the same as ';' (see its own comment
 * on why that change was needed for this file to work at all: a
 * multi-line body handed to run_line() as one string would otherwise
 * tokenize into a single, wrong command instead of a sequence of
 * them).
 *
 * Not yet handling for/while/case/functions -- see the project's own
 * README/roadmap. Only "if" is recognized right now.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "control.h"

#define MAX_BRANCHES 16
#define MAX_MARKERS  (MAX_BRANCHES * 3)

enum marker_kind_e
{
  MARKER_THEN,
  MARKER_ELIF,
  MARKER_ELSE,
  MARKER_FI
};

struct marker_s
{
    enum marker_kind_e kind;
    FAR char *start; /* start of the keyword itself */
    FAR char *after; /* just after the keyword */
};

struct branch_s
{
    FAR char *cond; /* NULL for the final "else" branch (always runs) */
    FAR char *body;
};

static bool is_ident_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* A real shell word boundary -- whitespace, ';', newline, or start/
 * end of string. Deliberately *not* "not an identifier character":
 * confirmed directly, the hard way, that using !is_ident_char() here
 * instead treats punctuation like '-' as a boundary too, which is
 * wrong -- "echo multiline-then-ok" is one ordinary word in real
 * shells (word-splitting is whitespace-based, not punctuation-based),
 * but checking only "is the previous character not a letter/digit/
 * underscore" saw the '-' before "then" and happily treated it as a
 * fresh word start, matching the real keyword "then" that's actually
 * just embedded, harmlessly, inside a longer argument.
 */

static bool is_word_boundary(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == ';' || c == '\0';
}

/* Skips leading whitespace/newlines, then the leading "if" keyword
 * itself -- shared by find_markers() and build_branches(), both of
 * which need to start scanning right after it. Confirmed directly
 * this needs the leading-whitespace skip specifically for *nested*
 * constructs: an outer if's own extracted body text starts with the
 * newline that followed its "then" (e.g. "\n    if false\n..."), not
 * with "if" as the very first character the way a fresh, top-level
 * construct's text always does.
 */

static FAR char *skip_leading_if(FAR char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    {
        p++;
    }

    while (is_ident_char(*p))
    {
        p++;
    }

    return p;
}

bool is_control_start(FAR const char *line)
{
    FAR const char *p = line;

    /* Skips '\n'/'\r' too, not just ' '/'\t' -- confirmed directly
     * this matters for a *nested* if specifically: build_branches()'s
     * own extracted body text for an outer if/then starts right
     * after that "then" keyword, which means it starts with the very
     * newline that followed it in the original text (e.g. "then\n
     * if false\n..." becomes a body of "\n    if false\n...") --
     * without skipping that leading newline here too, this check
     * would look at '\n' itself instead of the "if" that follows it,
     * and never recognize the nested construct as one at all.
     */

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    {
        p++;
    }

    if (strncmp(p, "if", 2) != 0)
    {
        return false;
    }

    return is_word_boundary(p[2]);
}

/****************************************************************************
 * Net change in if-nesting depth this text causes: +1 per whole-word
 * "if", -1 per whole-word "fi", ignoring anything inside quotes.
 * Shared by both the accumulation loop (deciding when enough input
 * has been read) and find_markers() below (deciding which "then"/
 * "elif"/"else"/"fi" belong to *this* if, not a nested one).
 ****************************************************************************/

static int depth_delta(FAR const char *text)
{
    int delta = 0;
    char quote = '\0';
    FAR const char *p = text;

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

        if (is_ident_char(*p) && (p == text || is_word_boundary(p[-1])))
        {
            FAR const char *word_start = p;

            while (is_ident_char(*p))
            {
                p++;
            }

            if (!is_word_boundary(*p))
            {
                /* Extracted word is followed immediately by more,
                 * non-identifier text of the same word (e.g. "if-
                 * something") -- not a real, standalone "if"/"fi",
                 * same reasoning as the before-check above.
                 */

                continue;
            }

            if (p - word_start == 2 && strncmp(word_start, "if", 2) == 0)
            {
                delta++;
            }
            else if (p - word_start == 2 && strncmp(word_start, "fi", 2) == 0)
            {
                delta--;
            }

            continue;
        }


        p++;
    }

    return delta;
}

static bool append_line(FAR char **buf, FAR const char *more)
{
    size_t oldlen = (*buf != NULL) ? strlen(*buf) : 0;
    size_t addlen = strlen(more);
    FAR char *grown = realloc(*buf, oldlen + addlen + 2);

    if (grown == NULL)
    {
        return false;
    }

    *buf = grown;
    memcpy(*buf + oldlen, more, addlen);
    (*buf)[oldlen + addlen] = '\n';
    (*buf)[oldlen + addlen + 1] = '\0';
    return true;
}

/****************************************************************************
 * Keeps calling read_line() until the if this text started has a
 * matching "fi" (tracking nested if/fi pairs via depth_delta() above,
 * so a nested if's own "fi" doesn't end the outer construct early).
 * Takes ownership of first_line. Returns NULL (having freed
 * everything already read) on EOF before the construct closed, or on
 * allocation failure.
 ****************************************************************************/

static FAR char *accumulate_construct(FAR char *first_line,
                                       next_line_fn read_line, FAR void *ctx)
{
    FAR char *text = strdup(first_line);
    int depth;

    free(first_line);

    if (text == NULL)
    {
        return NULL;
    }

    depth = depth_delta(text);

    while (depth > 0)
    {
        FAR char *more = read_line(ctx, true);

        if (more == NULL)
        {
            free(text);
            return NULL;
        }

        depth += depth_delta(more);

        if (!append_line(&text, more))
        {
            free(more);
            free(text);
            return NULL;
        }

        free(more);
    }

    return text;
}

/****************************************************************************
 * Records every top-level (depth == 1, i.e. belonging to the
 * outermost if this text starts with, not a nested one) "then"/
 * "elif"/"else"/"fi" in order. Assumes 'text' already starts with
 * "if" and is fully balanced (accumulate_construct() guarantees
 * both).
 ****************************************************************************/

static int find_markers(FAR char *text, struct marker_s markers[],
                         int max_markers)
{
    int count = 0;
    int depth = 1;
    char quote = '\0';
    FAR char *p = text;

    p = skip_leading_if(p);

    while (*p != '\0' && depth > 0)
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

        if (is_ident_char(*p) && (p == text || is_word_boundary(p[-1])))
        {
            FAR char *word_start = p;
            size_t wordlen;

            while (is_ident_char(*p))
            {
                p++;
            }

            wordlen = (size_t)(p - word_start);

            if (!is_word_boundary(*p))
            {
                /* Same reasoning as depth_delta()'s own identical
                 * check: a word like "if-something" or "then2" isn't
                 * a real, standalone keyword just because it starts
                 * with one.
                 */

                continue;
            }

            if (wordlen == 2 && strncmp(word_start, "if", 2) == 0)
            {
                depth++;
            }
            else if (wordlen == 2 && strncmp(word_start, "fi", 2) == 0)
            {
                depth--;
                if (depth == 0 && count < max_markers)
                {
                    markers[count].kind = MARKER_FI;
                    markers[count].start = word_start;
                    markers[count].after = p;
                    count++;
                }
            }
            else if (depth == 1 && count < max_markers)
            {
                if (wordlen == 4 && strncmp(word_start, "then", 4) == 0)
                {
                    markers[count].kind = MARKER_THEN;
                    markers[count].start = word_start;
                    markers[count].after = p;
                    count++;
                }
                else if (wordlen == 4 && strncmp(word_start, "elif", 4) == 0)
                {
                    markers[count].kind = MARKER_ELIF;
                    markers[count].start = word_start;
                    markers[count].after = p;
                    count++;
                }
                else if (wordlen == 4 && strncmp(word_start, "else", 4) == 0)
                {
                    markers[count].kind = MARKER_ELSE;
                    markers[count].start = word_start;
                    markers[count].after = p;
                    count++;
                }
            }

            continue;
        }

        p++;
    }

    return count;
}

/****************************************************************************
 * Returns a pointer just past the matching "fi" that closes the if
 * construct 'text' starts with, or NULL if 'text' doesn't actually
 * contain a complete, balanced one. Lets run_line() (line.c) treat
 * "if ...; fi <more statements>" correctly: the construct itself ends
 * at that "fi", and whatever comes after it is separate, subsequent
 * text to keep processing, not part of the construct -- confirmed
 * directly this matters for a *nested* if specifically, whose own
 * body can contain "if ... fi" followed by further statements before
 * the *outer* body's own end (e.g. "if false\nthen ...\nfi\necho
 * after\n" as one outer branch's body) -- treating the entire given
 * text as one all-consuming construct silently discarded that
 * trailing "echo after" instead of running it.
 ****************************************************************************/

FAR char *find_construct_end(FAR char *text)
{
    struct marker_s markers[MAX_MARKERS];
    int nmarkers = find_markers(text, markers, MAX_MARKERS);

    if (nmarkers == 0 || markers[nmarkers - 1].kind != MARKER_FI)
    {
        return NULL;
    }

    return markers[nmarkers - 1].after;
}

/****************************************************************************
 * Turns the marker list into cond/body pairs: branches[i].cond is the
 * text between "if"/"elif" and its own "then"; branches[i].body is
 * the text between that "then" and whatever comes next (another
 * "elif", an "else", or the closing "fi"). A trailing "else" becomes
 * one final branch with cond == NULL, meaning "always runs if
 * execution gets this far". Malformed marker sequences (a "then"
 * expected but something else found) stop cleanly, running whatever
 * branches were already parsed correctly rather than misinterpreting
 * the rest.
 ****************************************************************************/

static int build_branches(FAR char *text, struct marker_s markers[],
                           int nmarkers, struct branch_s branches[],
                           int max_branches)
{
    int nbranches = 0;
    FAR char *p = text;
    int mi = 0;

    p = skip_leading_if(p);

    while (mi < nmarkers && nbranches < max_branches)
    {
        size_t condlen;
        FAR char *body_end;
        size_t bodylen;

        if (markers[mi].kind != MARKER_THEN)
        {
            break;
        }

        condlen = (size_t)(markers[mi].start - p);
        branches[nbranches].cond = malloc(condlen + 1);
        if (branches[nbranches].cond == NULL)
        {
            return nbranches;
        }

        memcpy(branches[nbranches].cond, p, condlen);
        branches[nbranches].cond[condlen] = '\0';

        p = markers[mi].after;
        mi++;

        body_end = (mi < nmarkers) ? markers[mi].start : (text + strlen(text));
        bodylen = (size_t)(body_end - p);
        branches[nbranches].body = malloc(bodylen + 1);
        if (branches[nbranches].body == NULL)
        {
            free(branches[nbranches].cond);
            return nbranches;
        }

        memcpy(branches[nbranches].body, p, bodylen);
        branches[nbranches].body[bodylen] = '\0';
        nbranches++;

        if (mi >= nmarkers || markers[mi].kind == MARKER_FI)
        {
            break;
        }

        if (markers[mi].kind == MARKER_ELSE)
        {
            p = markers[mi].after;
            mi++;

            if (mi < nmarkers && nbranches < max_branches)
            {
                body_end = markers[mi].start; /* the closing "fi" */
                bodylen = (size_t)(body_end - p);
                branches[nbranches].cond = NULL;
                branches[nbranches].body = malloc(bodylen + 1);
                if (branches[nbranches].body != NULL)
                {
                    memcpy(branches[nbranches].body, p, bodylen);
                    branches[nbranches].body[bodylen] = '\0';
                    nbranches++;
                }
            }

            break;
        }

        if (markers[mi].kind == MARKER_ELIF)
        {
            p = markers[mi].after;
            mi++;
            continue;
        }

        break;
    }

    return nbranches;
}

static int run_branches(struct branch_s branches[], int nbranches,
                         FAR bool *should_exit)
{
    int i;

    for (i = 0; i < nbranches; i++)
    {
        if (branches[i].cond == NULL)
        {
            return run_line(branches[i].body, should_exit);
        }

        {
            int cond_status = run_line(branches[i].cond, should_exit);

            if (*should_exit)
            {
                return cond_status;
            }

            if (cond_status == 0)
            {
                return run_line(branches[i].body, should_exit);
            }
        }
    }

    /* No branch matched, and no "else" -- real shells make this a
     * successful (status 0) no-op, not a failure.
     */

    return 0;
}

int run_control_construct(FAR char *first_line, next_line_fn read_line,
                           FAR void *ctx, FAR bool *should_exit)
{
    FAR char *text;
    FAR char *construct_end;
    FAR char *construct_copy;
    size_t construct_len;
    struct marker_s markers[MAX_MARKERS];
    struct branch_s branches[MAX_BRANCHES];
    int nmarkers;
    int nbranches;
    int status;
    int i;

    *should_exit = false;

    text = accumulate_construct(first_line, read_line, ctx);
    if (text == NULL)
    {
        fprintf(stderr,
                "vaporshell: unexpected end of input looking for matching "
                "'fi'\n");
        return 1;
    }

    /* accumulate_construct() only guarantees 'text' *contains* a
     * complete, balanced construct -- not that the construct is all
     * there is. "if ...; fi; echo after" (all on one line) and a
     * nested if's own body containing "if ... fi" followed by more
     * statements both need the construct itself separated from
     * whatever text follows its closing "fi", which is run
     * afterward, separately, via run_line() -- not treated as part of
     * this construct at all. This is the one place that split has to
     * happen: every caller (line.c's own nested-if handling, and
     * both vaporshell_main.c's interactive loop and script.c) goes
     * through this same function now, specifically so this logic
     * lives once, here, rather than being duplicated (and, before
     * this, inconsistently applied) in each of them.
     */

    construct_end = find_construct_end(text);
    if (construct_end == NULL)
    {
        construct_end = text + strlen(text);
    }

    construct_len = (size_t)(construct_end - text);
    construct_copy = malloc(construct_len + 1);
    if (construct_copy == NULL)
    {
        free(text);
        return 1;
    }

    memcpy(construct_copy, text, construct_len);
    construct_copy[construct_len] = '\0';

    nmarkers = find_markers(construct_copy, markers, MAX_MARKERS);
    nbranches = build_branches(construct_copy, markers, nmarkers, branches,
                                MAX_BRANCHES);

    status = run_branches(branches, nbranches, should_exit);

    for (i = 0; i < nbranches; i++)
    {
        free(branches[i].cond);
        free(branches[i].body);
    }

    free(construct_copy);

    if (!*should_exit)
    {
        FAR char *remainder = construct_end;

        while (*remainder == ' ' || *remainder == '\t' ||
               *remainder == ';' || *remainder == '\n' ||
               *remainder == '\r')
        {
            remainder++;
        }

        if (*remainder != '\0')
        {
            FAR char *remainder_copy = strdup(remainder);

            if (remainder_copy != NULL)
            {
                status = run_line(remainder_copy, should_exit);
                free(remainder_copy);
            }
        }
    }

    free(text);
    return status;
}
