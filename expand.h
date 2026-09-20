/*
 * expand.h -- word expansion and pattern matching.
 *
 * A word is expanded in POSIX order: tilde, parameter / command /
 * arithmetic expansion, field splitting, pathname expansion, quote
 * removal. One raw word can yield any number of fields, which is why
 * the result is a vector rather than a string.
 */

#ifndef VAPORSHELL_EXPAND_H
#define VAPORSHELL_EXPAND_H

#include <stdbool.h>
#include <stddef.h>

#include "ast.h"

struct fieldv_s
{
  char **v;
  int n;
  int cap;
};

void fv_init(struct fieldv_s *f);
void fv_add(struct fieldv_s *f, char *owned);   /* takes ownership */
void fv_free(struct fieldv_s *f);

/* A pattern is text plus a parallel array marking characters that were
 * quoted (and so match literally). q may be NULL: nothing quoted.
 */

struct pat_s
{
  char *s;
  char *q;
  size_t len;
};

/* expand.c */

int expand_words(const struct word_s *w, struct fieldv_s *out);
void vs_brace_expand(const char *word, struct fieldv_s *out);      /* brace.c */
char *expand_word_str(const char *raw);       /* no split, no glob */
char *expand_assign_str(const char *raw);     /* also ~ after ':' and '=' */
char *expand_heredoc(const char *body);
struct pat_s expand_pattern(const char *raw);
void pat_free(struct pat_s *p);

/* Runs a command substitution's text and returns its output with
 * trailing newlines stripped (exec.c).
 */

char *run_cmdsub(const char *text, size_t len);

/* arith.c: returns 0 and stores the value, or -1 after printing why. */

int arith_eval(const char *expr, long *result);

/* glob.c */

bool pat_match(const char *p, const char *pq, size_t plen, const char *str);
bool pat_match_ci(const char *p, const char *pq, size_t plen, const char *str, bool ci);
bool pat_has_glob(const char *p, const char *pq, size_t plen);
int glob_expand(const char *p, const char *pq, size_t plen,
                struct fieldv_s *out);

#endif
