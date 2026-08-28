/*
 * control.c -- shared control-flow infrastructure (keyword/depth/
 * quote-aware scanning, multi-line accumulation, separating a
 * construct from whatever text follows its own closing keyword) plus
 * "if"/"then"/"elif"/"else"/"fi" itself. "for"/"while"/"until" live in
 * loops.c, "case" in case.c -- both reuse this file's own shared
 * scanning (control_internal.h) rather than reimplementing it.
 *
 * Works identically whether a construct is all on one line
 * (';'-separated) or spans multiple (the common script style) --
 * both are handled the same way, since line.c's own split_line() now
 * treats '\n' the same as ';' (see its own comment on why that change
 * was needed for any of this to work at all: a multi-line body handed
 * to run_line() as one string would otherwise tokenize into a single,
 * wrong command instead of a sequence of them).
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "control.h"
#include "control_internal.h"

#define MAX_BRANCHES 16

struct branch_s
{
    FAR char *cond; /* NULL for the final "else" branch (always runs) */
    FAR char *body;
};

bool is_ident_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

bool is_word_boundary(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == ';' || c == '\0';
}

bool starts_with_word(FAR const char *text, FAR const char *word)
{
    FAR const char *p = text;
    size_t wordlen = strlen(word);

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    {
        p++;
    }

    return strncmp(p, word, wordlen) == 0 && is_word_boundary(p[wordlen]);
}

FAR char *skip_leading_keyword(FAR char *p)
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
    static const FAR char *const keywords[] =
    {
        "if", "for", "while", "until", "case", NULL
    };
    int i;

    for (i = 0; keywords[i] != NULL; i++)
    {
        if (starts_with_word(line, keywords[i]))
        {
            return true;
        }
    }

    return false;
}

/* Every construct keyword that opens a new nesting level -- shared by
 * depth_delta() (deciding when enough input has been read),
 * find_markers() (deciding which markers belong to *this* construct,
 * not a nested one), and case.c's own pattern-scanning pass (a nested
 * construct within one of a case's own bodies still needs its own
 * ')'/'"'/'\'' etc. correctly skipped over, not mistaken for case's
 * own pattern/body delimiters).
 */

bool is_opener(FAR const char *word, size_t len)
{
    return (len == 2 && strncmp(word, "if", 2) == 0) ||
           (len == 3 && strncmp(word, "for", 3) == 0) ||
           (len == 5 && strncmp(word, "while", 5) == 0) ||
           (len == 5 && strncmp(word, "until", 5) == 0) ||
           (len == 4 && strncmp(word, "case", 4) == 0);
}

/* Every keyword that closes one -- "fi" closes "if", "done" closes
 * "for"/"while"/"until", "esac" closes "case". *kind_out is set to
 * which one matched, for find_markers()'s own benefit (it needs to
 * know which closer actually ended the construct, not just that some
 * closer did).
 */

bool is_closer(FAR const char *word, size_t len,
               FAR enum marker_kind_e *kind_out)
{
    if (len == 2 && strncmp(word, "fi", 2) == 0)
    {
        *kind_out = MARKER_FI;
        return true;
    }

    if (len == 4 && strncmp(word, "done", 4) == 0)
    {
        *kind_out = MARKER_DONE;
        return true;
    }

    if (len == 4 && strncmp(word, "esac", 4) == 0)
    {
        *kind_out = MARKER_ESAC;
        return true;
    }

    return false;
}

/* True for keywords immediately followed by a real command --
 * "if"/"elif"/"while"/"until" by their own condition, "do" by a
 * loop's first body statement, "then"/"else" by an if's own first
 * branch statement. Used to decide whether the *next* word scanned is
 * itself at a "command start" position, where recognizing if/for/
 * while/until/case/fi/done/esac as meaningful keywords is actually
 * correct -- confirmed directly, the hard way, that recognizing them
 * *anywhere* a whole word happens to match is wrong: comparing a
 * variable against the literal string "done" (`[ "$x" = done ]`, an
 * entirely ordinary, common pattern -- confirmed directly this
 * breaks a real "until" loop written exactly that way) was being
 * misread as the real closing "done" keyword, since nothing
 * distinguished "keyword in command position" from "keyword as an
 * ordinary value inside some other command's own arguments". Every
 * other keyword ("for", "case", "in", and any ordinary, non-keyword
 * word) is not immediately followed by a new command in that same
 * sense, so command-start tracking turns off after them -- "for"/
 * "case"/"in" are followed by a list/pattern, not a command; ";"/
 * newline/"&&"/"||" (handled separately, not here) already reopen
 * command-start position for whatever comes after "fi"/"done"/"esac"
 * anyway, so those three don't need to appear here either.
 */

static bool starts_new_command(FAR const char *word, size_t len)
{
    return (len == 2 && strncmp(word, "if", 2) == 0) ||
           (len == 4 && strncmp(word, "elif", 4) == 0) ||
           (len == 5 && strncmp(word, "while", 5) == 0) ||
           (len == 5 && strncmp(word, "until", 5) == 0) ||
           (len == 2 && strncmp(word, "do", 2) == 0) ||
           (len == 4 && strncmp(word, "then", 4) == 0) ||
           (len == 4 && strncmp(word, "else", 4) == 0);
}

/****************************************************************************
 * Net change in nesting depth this text causes: +1 per whole-word
 * opener (if/for/while/until/case), -1 per whole-word closer (fi/
 * done/esac), ignoring anything inside quotes, and only counting a
 * match at all when it's actually in "command start" position (see
 * starts_new_command()'s own comment for why that check exists).
 * Shared by the accumulation loop below and find_markers()
 * (control_internal.h) -- both need the identical logic, or the same
 * misreading bug would just reappear in whichever one lacked it.
 ****************************************************************************/

static int depth_delta(FAR const char *text)
{
    int delta = 0;
    char quote = '\0';
    FAR const char *p = text;
    bool at_command_start = true;

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
            at_command_start = false;
            continue;
        }

        if (*p == ';' || *p == '\n')
        {
            at_command_start = true;
            p++;
            continue;
        }

        if ((*p == '&' && p[1] == '&') || (*p == '|' && p[1] == '|'))
        {
            at_command_start = true;
            p += 2;
            continue;
        }

        if (*p == ' ' || *p == '\t' || *p == '\r')
        {
            p++;
            continue;
        }

        if (is_ident_char(*p) && (p == text || is_word_boundary(p[-1])))
        {
            FAR const char *word_start = p;
            size_t wordlen;
            enum marker_kind_e unused_kind;
            bool this_word_starts_command = at_command_start;

            while (is_ident_char(*p))
            {
                p++;
            }

            wordlen = (size_t)(p - word_start);

            if (!is_word_boundary(*p))
            {
                /* Extracted word is followed immediately by more,
                 * non-identifier text of the same word (e.g. "if-
                 * something") -- not a real, standalone keyword, same
                 * reasoning as the before-check above.
                 */

                at_command_start = false;
                continue;
            }

            if (this_word_starts_command)
            {
                if (is_opener(word_start, wordlen))
                {
                    delta++;
                }
                else if (is_closer(word_start, wordlen, &unused_kind))
                {
                    delta--;
                }
            }

            at_command_start = this_word_starts_command &&
                                starts_new_command(word_start, wordlen);
            continue;
        }

        at_command_start = false;
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
 * Keeps calling read_line() until the construct this text started has
 * a matching closer (tracking nested opener/closer pairs via
 * depth_delta() above, so a nested construct's own closer doesn't end
 * the outer one early). Takes ownership of first_line. Returns NULL
 * (having freed everything already read) on EOF before the construct
 * closed, or on allocation failure.
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

int find_markers(FAR char *text, struct marker_s markers[], int max_markers)
{
    int count = 0;
    int depth = 1;
    char quote = '\0';
    FAR char *p = text;
    bool at_command_start = true;

    p = skip_leading_keyword(p);

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
            at_command_start = false;
            continue;
        }

        if (*p == ';' || *p == '\n')
        {
            at_command_start = true;
            p++;
            continue;
        }

        if ((*p == '&' && p[1] == '&') || (*p == '|' && p[1] == '|'))
        {
            at_command_start = true;
            p += 2;
            continue;
        }

        if (*p == ' ' || *p == '\t' || *p == '\r')
        {
            p++;
            continue;
        }

        if (is_ident_char(*p) && (p == text || is_word_boundary(p[-1])))
        {
            FAR char *word_start = p;
            size_t wordlen;
            enum marker_kind_e closer_kind;
            bool this_word_starts_command = at_command_start;

            while (is_ident_char(*p))
            {
                p++;
            }

            wordlen = (size_t)(p - word_start);

            if (!is_word_boundary(*p))
            {
                at_command_start = false;
                continue;
            }

            /* this_word_starts_command only gates is_opener()/
             * is_closer() below -- "then"/"elif"/"else"/"in"/"do"
             * (the else-if branch further down) are deliberately NOT
             * gated the same way: unlike if/fi/done/esac, they're not
             * themselves preceded by a command-start-triggering
             * keyword in real grammar -- "in" follows an ordinary
             * word (a for-loop's own variable name, or case's own
             * word expression), "do" follows a condition or list, not
             * a keyword. Gating them the same way as openers/closers
             * broke recognizing "in"/"do" entirely, confirmed
             * directly building and running against a real for loop.
             */

            if (is_opener(word_start, wordlen))
            {
                if (this_word_starts_command)
                {
                    depth++;
                }
            }
            else if (is_closer(word_start, wordlen, &closer_kind))
            {
                if (this_word_starts_command)
                {
                    depth--;
                    if (depth == 0 && count < max_markers)
                    {
                        markers[count].kind = closer_kind;
                        markers[count].start = word_start;
                        markers[count].after = p;
                        count++;
                    }
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
                else if (wordlen == 2 && strncmp(word_start, "in", 2) == 0)
                {
                    markers[count].kind = MARKER_IN;
                    markers[count].start = word_start;
                    markers[count].after = p;
                    count++;
                }
                else if (wordlen == 2 && strncmp(word_start, "do", 2) == 0)
                {
                    markers[count].kind = MARKER_DO;
                    markers[count].start = word_start;
                    markers[count].after = p;
                    count++;
                }
            }

            at_command_start = starts_new_command(word_start, wordlen);
            continue;
        }

        at_command_start = false;
        p++;
    }

    return count;
}

/****************************************************************************
 * Returns a pointer just past the matching closer ("fi"/"done"/
 * "esac") for the construct 'text' starts with, or NULL if 'text'
 * doesn't actually contain a complete, balanced one. Lets a construct
 * be separated from whatever text follows its own close on the same
 * line/string -- e.g. "if ...; fi; echo after" -- confirmed directly
 * this matters, the hard way: treating the entire given text as one
 * all-consuming construct silently discarded anything after the
 * close instead of running it.
 ****************************************************************************/

FAR char *find_construct_end(FAR char *text)
{
    struct marker_s markers[MAX_MARKERS];
    int nmarkers = find_markers(text, markers, MAX_MARKERS);
    enum marker_kind_e last_kind;

    if (nmarkers == 0)
    {
        return NULL;
    }

    last_kind = markers[nmarkers - 1].kind;

    if (last_kind != MARKER_FI && last_kind != MARKER_DONE &&
        last_kind != MARKER_ESAC)
    {
        return NULL;
    }

    return markers[nmarkers - 1].after;
}

/****************************************************************************
 * Turns the marker list into cond/body pairs for "if": branches[i].cond
 * is the text between "if"/"elif" and its own "then"; branches[i].body
 * is the text between that "then" and whatever comes next (another
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

    p = skip_leading_keyword(p);

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

static int run_if(FAR char *construct_copy, FAR bool *should_exit)
{
    struct marker_s markers[MAX_MARKERS];
    struct branch_s branches[MAX_BRANCHES];
    int nmarkers;
    int nbranches;
    int status;
    int i;

    nmarkers = find_markers(construct_copy, markers, MAX_MARKERS);
    nbranches = build_branches(construct_copy, markers, nmarkers, branches,
                                MAX_BRANCHES);

    status = run_branches(branches, nbranches, should_exit);

    for (i = 0; i < nbranches; i++)
    {
        free(branches[i].cond);
        free(branches[i].body);
    }

    return status;
}

int run_control_construct(FAR char *first_line, next_line_fn read_line,
                           FAR void *ctx, FAR bool *should_exit)
{
    FAR char *text;
    FAR char *construct_end;
    FAR char *construct_copy;
    size_t construct_len;
    int status;

    *should_exit = false;

    text = accumulate_construct(first_line, read_line, ctx);
    if (text == NULL)
    {
        fprintf(stderr,
                "vaporshell: unexpected end of input looking for matching "
                "'fi'/'done'/'esac'\n");
        return 1;
    }

    /* accumulate_construct() only guarantees 'text' *contains* a
     * complete, balanced construct -- not that the construct is all
     * there is. The construct itself has to be separated from
     * whatever text follows its own close, which is run afterward,
     * separately, via run_line() -- not treated as part of this
     * construct at all. This is the one place that split happens:
     * every caller (line.c's own nested-construct handling, and both
     * vaporshell_main.c's interactive loop and script.c) goes through
     * this same function now, specifically so this logic lives once,
     * here.
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

    if (starts_with_word(construct_copy, "if"))
    {
        status = run_if(construct_copy, should_exit);
    }
    else if (starts_with_word(construct_copy, "for"))
    {
        status = run_for(construct_copy, should_exit);
    }
    else if (starts_with_word(construct_copy, "while"))
    {
        status = run_while_until(construct_copy, should_exit, true);
    }
    else if (starts_with_word(construct_copy, "until"))
    {
        status = run_while_until(construct_copy, should_exit, false);
    }
    else if (starts_with_word(construct_copy, "case"))
    {
        status = run_case(construct_copy, should_exit);
    }
    else
    {
        /* is_control_start() already guaranteed this is one of the
         * five keywords above -- shouldn't be reachable.
         */

        status = 1;
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
