#ifndef VAPORSHELL_CONTROL_INTERNAL_H
#define VAPORSHELL_CONTROL_INTERNAL_H

/*
 * control_internal.h -- shared scanning primitives for every control-
 * flow construct (control.c's own "if", loops.c's "for"/"while"/
 * "until", case.c's "case"). Not included outside this small family
 * of files -- vaporshell.h/control.h are the real, public interface
 * the rest of the shell uses; this is just the shared plumbing behind
 * them, split out specifically so loops.c/case.c don't each
 * reimplement the same keyword/depth/quote-aware scanning control.c
 * already got right (and debugged, the hard way) for "if".
 */

#include "vaporshell.h"

#define MAX_MARKERS 64

enum marker_kind_e
{
    MARKER_THEN,
    MARKER_ELIF,
    MARKER_ELSE,
    MARKER_IN,
    MARKER_DO,
    MARKER_DONE,
    MARKER_FI,
    MARKER_ESAC
};

struct marker_s
{
    enum marker_kind_e kind;
    FAR char *start; /* start of the keyword itself */
    FAR char *after;  /* just after the keyword */
};

bool is_ident_char(char c);

/* A real shell word boundary -- whitespace, ';', newline, or start/
 * end of string. Deliberately *not* "not an identifier character":
 * confirmed directly, the hard way, that using !is_ident_char()
 * instead treats punctuation like '-' as a boundary too, which is
 * wrong -- word-splitting in real shells is whitespace-based, not
 * punctuation-based.
 */

bool is_word_boundary(char c);

/* True iff 'text' (leading whitespace/newlines skipped) starts with
 * 'word' as a whole word -- e.g. starts_with_word("for x in", "for")
 * is true, starts_with_word("format", "for") is false.
 */

bool starts_with_word(FAR const char *text, FAR const char *word);

/* Skips leading whitespace/newlines, then one leading identifier word
 * (whichever construct keyword text starts with -- "if"/"for"/
 * "while"/"until"/"case") -- shared by every construct's own marker-
 * scanning, all of which need to start right after it. Confirmed
 * directly this needs the leading-whitespace skip specifically for
 * *nested* constructs: an outer construct's own extracted body text
 * starts with the newline that followed its "then"/"do"/etc, not with
 * the keyword as the very first character the way fresh, top-level
 * input always does.
 */

FAR char *skip_leading_keyword(FAR char *p);

/* Shared opener/closer keyword checks -- see control.c's own comment
 * on these for the full reasoning. Exposed here specifically for
 * case.c's own pattern-scanning pass, which needs to track nesting
 * depth too (a nested construct inside one of a case's own bodies
 * still needs its own delimiters correctly skipped over).
 */

bool is_opener(FAR const char *word, size_t len);
bool is_closer(FAR const char *word, size_t len,
               FAR enum marker_kind_e *kind_out);

/* Records every top-level (depth == 1, i.e. belonging to the
 * outermost construct 'text' starts with, not a nested one) "then"/
 * "elif"/"else"/"in"/"do", plus whichever closer ("fi"/"done"/"esac")
 * actually closes it, in order. Assumes 'text' already starts with a
 * valid construct keyword and is fully balanced. Every construct
 * type's own runner calls this same function and interprets whichever
 * markers are relevant to its own grammar, ignoring the rest.
 */

int find_markers(FAR char *text, struct marker_s markers[], int max_markers);

/* loops.c */
int run_for(FAR char *construct_copy, FAR bool *should_exit);
int run_while_until(FAR char *construct_copy, FAR bool *should_exit,
                     bool is_while);

/* case.c */
int run_case(FAR char *construct_copy, FAR bool *should_exit);

#endif
