/*
 * case.c -- case/esac. Structurally the most different of the
 * control-flow constructs: its own internal item delimiters (')' and
 * ';;') are punctuation, not identifier keywords, so find_markers()
 * (control_internal.h) only gets this file as far as locating "in"
 * and the closing "esac" -- everything between them needs its own,
 * separate scanning pass (find_case_items(), below), which still has
 * to respect quotes and nested constructs the same way (an if/fi
 * inside one of case's own bodies shouldn't have its own punctuation
 * mistaken for case's own item delimiters).
 *
 * Pattern matching supports literal text, '*' (any sequence,
 * including empty) and '?' (exactly one character), plus '|' for
 * alternation between patterns for the same body -- covers the large
 * majority of real case usage (a bare "*)" default case, "yes|Y)",
 * "*.txt)", ...). Bracket expressions ("[abc]"/"[a-z]") are a real,
 * known gap, not implemented here.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "expand.h"
#include "control_internal.h"

#define MAX_CASE_ITEMS 32

struct case_item_s
{
    FAR char *pattern;
    FAR char *body;
};

/****************************************************************************
 * Standard recursive glob match: '*' matches any sequence (including
 * empty), '?' matches exactly one character, anything else must match
 * literally.
 ****************************************************************************/

static bool glob_match(FAR const char *pattern, FAR const char *text)
{
    while (*pattern != '\0')
    {
        if (*pattern == '*')
        {
            while (*pattern == '*')
            {
                pattern++;
            }

            if (*pattern == '\0')
            {
                return true;
            }

            while (*text != '\0')
            {
                if (glob_match(pattern, text))
                {
                    return true;
                }

                text++;
            }

            return glob_match(pattern, text);
        }
        else if (*pattern == '?')
        {
            if (*text == '\0')
            {
                return false;
            }

            pattern++;
            text++;
        }
        else
        {
            if (*text != *pattern)
            {
                return false;
            }

            pattern++;
            text++;
        }
    }

    return *text == '\0';
}

/****************************************************************************
 * Splits 'region' (the text between "in" and the closing "esac",
 * exclusive of both) into pattern/body pairs on ')' and ';;',
 * tracking quotes and construct nesting depth the whole way -- a
 * nested if/for/while/until/case within one of case's own bodies gets
 * its own ')'/';;'-like punctuation correctly skipped over rather than
 * mistaken for this case's own item boundaries. The last item may
 * omit its own trailing ';;' before the region ends (valid POSIX,
 * handled after the main loop).
 ****************************************************************************/

static int find_case_items(FAR char *region, struct case_item_s items[],
                            int max_items)
{
    int count = 0;
    FAR char *p = region;
    FAR char *item_start = region;
    FAR char *pattern_end = NULL;
    char quote = '\0';
    int depth = 0;

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

        if (is_ident_char(*p) && (p == region || is_word_boundary(p[-1])))
        {
            FAR char *word_start = p;
            size_t wordlen;
            enum marker_kind_e unused_kind;

            while (is_ident_char(*p))
            {
                p++;
            }

            wordlen = (size_t)(p - word_start);

            if (is_word_boundary(*p))
            {
                if (is_opener(word_start, wordlen))
                {
                    depth++;
                }
                else if (is_closer(word_start, wordlen, &unused_kind))
                {
                    depth--;
                }
            }

            continue;
        }

        if (depth == 0 && pattern_end == NULL && *p == ')')
        {
            pattern_end = p;
            p++;
            continue;
        }

        if (depth == 0 && pattern_end != NULL && *p == ';' && p[1] == ';')
        {
            size_t patlen = (size_t)(pattern_end - item_start);
            FAR char *body_start = pattern_end + 1;
            size_t bodylen = (size_t)(p - body_start);

            if (count < max_items)
            {
                items[count].pattern = malloc(patlen + 1);
                items[count].body = malloc(bodylen + 1);

                if (items[count].pattern != NULL && items[count].body != NULL)
                {
                    memcpy(items[count].pattern, item_start, patlen);
                    items[count].pattern[patlen] = '\0';
                    memcpy(items[count].body, body_start, bodylen);
                    items[count].body[bodylen] = '\0';
                    count++;
                }
            }

            p += 2;
            item_start = p;
            pattern_end = NULL;
            continue;
        }

        p++;
    }

    if (pattern_end != NULL && count < max_items)
    {
        size_t patlen = (size_t)(pattern_end - item_start);
        FAR char *body_start = pattern_end + 1;
        size_t bodylen = (size_t)(p - body_start);

        items[count].pattern = malloc(patlen + 1);
        items[count].body = malloc(bodylen + 1);

        if (items[count].pattern != NULL && items[count].body != NULL)
        {
            memcpy(items[count].pattern, item_start, patlen);
            items[count].pattern[patlen] = '\0';
            memcpy(items[count].body, body_start, bodylen);
            items[count].body[bodylen] = '\0';
            count++;
        }
    }

    return count;
}

/****************************************************************************
 * True iff 'word' matches any one of 'pattern_text's own '|'-separated
 * alternatives. Each alternative is tokenized (correctly stripping any
 * quotes) and expanded ($VAR references inside a pattern are valid,
 * if less common) before glob-matching -- one real, known
 * simplification here: since alternatives are split on raw text before
 * tokenizing, a quoted '|' meant literally (not as alternation) isn't
 * distinguished from a real one. Rare in practice; not implemented.
 ****************************************************************************/

static bool pattern_matches(FAR const char *pattern_text, FAR const char *word)
{
    FAR char *copy = strdup(pattern_text);
    FAR char *p;
    FAR char *alt_start;
    bool matched = false;

    if (copy == NULL)
    {
        return false;
    }

    p = copy;
    alt_start = copy;

    for (; ; p++)
    {
        if (*p == '|' || *p == '\0')
        {
            char saved = *p;
            FAR char *raw_tokens[4];
            bool no_expand[4];
            int ntok;

            *p = '\0';

            ntok = tokenize(alt_start, raw_tokens, no_expand, 4);
            if (ntok > 0)
            {
                FAR char *expanded = expand_token(raw_tokens[0], no_expand[0]);

                if (expanded != NULL)
                {
                    if (glob_match(expanded, word))
                    {
                        matched = true;
                    }

                    free(expanded);
                }
            }
            else if (*alt_start == '\0')
            {
                /* An empty alternative (e.g. a pattern list like
                 * "a||b") matches only an empty word.
                 */

                matched = matched || (word[0] == '\0');
            }

            if (matched || saved == '\0')
            {
                break;
            }

            alt_start = p + 1;
        }
    }

    free(copy);
    return matched;
}

int run_case(FAR char *construct_copy, FAR bool *should_exit)
{
    struct marker_s markers[MAX_MARKERS];
    struct case_item_s items[MAX_CASE_ITEMS];
    int nmarkers;
    int nitems;
    int mi = 0;
    FAR char *p;
    FAR char *word_start;
    FAR char *word_end;
    FAR char *region_start;
    FAR char *region_end;
    FAR char *word_text;
    FAR char *raw_tokens[MAX_TOKENS];
    bool no_expand[MAX_TOKENS];
    FAR char *word;
    int ntok;
    int status = 0;
    int i;

    nmarkers = find_markers(construct_copy, markers, MAX_MARKERS);
    p = skip_leading_keyword(construct_copy);

    while (mi < nmarkers && markers[mi].kind != MARKER_IN)
    {
        mi++;
    }

    if (mi >= nmarkers)
    {
        fprintf(stderr, "vaporshell: case: missing 'in'\n");
        return 1;
    }

    word_start = p;
    word_end = markers[mi].start;

    region_start = markers[mi].after;
    region_end = (mi + 1 < nmarkers) ? markers[mi + 1].start
                                      : (construct_copy + strlen(construct_copy));

    word_text = malloc((size_t)(word_end - word_start) + 1);
    if (word_text == NULL)
    {
        return 1;
    }

    memcpy(word_text, word_start, (size_t)(word_end - word_start));
    word_text[word_end - word_start] = '\0';

    ntok = tokenize(word_text, raw_tokens, no_expand, MAX_TOKENS);
    word = (ntok > 0) ? expand_token(raw_tokens[0], no_expand[0])
                       : strdup("");
    free(word_text);

    if (word == NULL)
    {
        return 1;
    }

    {
        size_t regionlen = (size_t)(region_end - region_start);
        FAR char *region = malloc(regionlen + 1);

        if (region == NULL)
        {
            free(word);
            return 1;
        }

        memcpy(region, region_start, regionlen);
        region[regionlen] = '\0';

        nitems = find_case_items(region, items, MAX_CASE_ITEMS);

        for (i = 0; i < nitems; i++)
        {
            if (pattern_matches(items[i].pattern, word))
            {
                status = run_line(items[i].body, should_exit);
                break;
            }
        }

        /* run_line() never takes ownership of/frees its own input
         * (confirmed directly: every real caller elsewhere in this
         * codebase -- main.c, script.c, line.c itself -- always frees
         * its own buffer after calling it), so every item's own text
         * still needs freeing here regardless of which one matched.
         */

        {
            int j;

            for (j = 0; j < nitems; j++)
            {
                free(items[j].pattern);
                free(items[j].body);
            }
        }

        free(region);
    }

    free(word);
    return status;
}
