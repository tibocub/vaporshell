/*
 * parse.h -- lexer and parser interface.
 *
 * Input arrives line by line from a callback, so the same code serves an
 * interactive prompt, a script file and an in-memory string. The lexer
 * asks for another line only when the construct it is in cannot end yet
 * (open quote, unfinished $(...), heredoc body, line continuation), which
 * is exactly when an interactive shell shows its continuation prompt.
 */

#ifndef VAPORSHELL_PARSE_H
#define VAPORSHELL_PARSE_H

#include <stdbool.h>
#include <stddef.h>

#include "ast.h"

/* Returns a malloc()'d line including its '\n', or NULL at end of input.
 * 'continuation' is false for the first line of a new command.
 */

typedef char *(*vs_line_fn)(void *ctx, bool continuation);

enum tok_e
{
  T_EOF,
  T_ERROR,
  T_NEWLINE,
  T_WORD,
  T_IO_NUMBER,
  T_ANDIF,                    /* && */
  T_ORIF,                     /* || */
  T_DSEMI,                    /* ;; */
  T_LESS,
  T_GREAT,
  T_DLESS,
  T_DGREAT,
  T_LESSAND,
  T_GREATAND,
  T_LESSGREAT,
  T_DLESSDASH,
  T_CLOBBER,
  T_SEMI,
  T_AMP,
  T_PIPE,
  T_LPAREN,
  T_RPAREN,
  T_SEMIAMP,                  /* ;&  (VF_BASH_SYNTAX) */
  T_DSEMIAMP,                 /* ;;& */
  T_TLESS,                    /* <<< */
  T_ANDGREAT,                 /* &>  (only when VF_AMP_REDIR) */
  T_ANDDGREAT                 /* &>> */
};

struct token_s
{
  enum tok_e type;
  char *text;                 /* T_WORD / T_IO_NUMBER, arena-allocated */
  bool quoted;                /* the word contained quoting characters */
  size_t start;               /* offsets into lexer buf: [start, end) */
  size_t end;
  int line;                   /* 1-based source line of the token */
};

struct hd_pending_s
{
  struct hd_pending_s *next;
  struct redir_s *redir;
};

struct lexer_s
{
  char *buf;
  size_t len;
  size_t cap;
  size_t pos;
  vs_line_fn getline;         /* NULL for in-memory text */
  void *ctx;
  bool fresh;                 /* the next fetched line starts a command */
  struct arena_s *arena;
  struct hd_pending_s *hd_head;   /* heredocs waiting for their bodies */
  char err[160];
  bool err_eof;               /* the error is "input ended too early" */
  bool force_extglob;         /* inside [[ ]]: extglob patterns are always words */
  int line_base;              /* newlines in text already dropped from buf */
  size_t lc_off;              /* cache: newlines counted up to this offset */
  int lc_line;
};

struct parser_s
{
  struct lexer_s lx;
  struct token_s tok;
  bool have;
  int depth;
  char err[160];
  bool err_eof;

  /* Alias expansion (see try_alias in parser.c). */

  bool no_alias;              /* nested look-ahead parsers must not splice */
  bool alias_blank;           /* the last alias ended in a blank */
  size_t alias_blank_pos;
  struct
  {
    const char *name;
    size_t end;               /* buf offset where this expansion's text ends */
  } aa[8];
  int naa;
};

enum parse_result_e
{
  PARSE_OK,                   /* *node may be NULL for an empty line */
  PARSE_EOF,
  PARSE_ERR                   /* message in p->err */
};

void parser_init(struct parser_s *p, vs_line_fn fn, void *ctx);
void parser_init_mem(struct parser_s *p, const char *text, size_t len);
void parser_free(struct parser_s *p);

/* Parses one complete command line. *arena receives a new arena the caller
 * must arena_release() (even for PARSE_OK with a NULL node).
 */

enum parse_result_e parse_command_line(struct parser_s *p,
                                       struct arena_s **arena,
                                       struct node_s **node);

/* After a syntax error: throw away the rest of the buffered input. */

void parser_discard(struct parser_s *p);

/* lexer.c */

void lexer_init(struct lexer_s *lx, vs_line_fn fn, void *ctx);
void lexer_free(struct lexer_s *lx);
int lex_token(struct lexer_s *lx, struct token_s *tok);
bool lex_fetch(struct lexer_s *lx);
void lex_add_heredoc(struct lexer_s *lx, struct redir_s *r);

/* wordscan.c: each returns WS_OK with *end just past the construct,
 * WS_INCOMPLETE if the text stops inside it, or WS_ERROR.
 */

enum { WS_OK = 0, WS_INCOMPLETE = 1, WS_ERROR = -1 };

int ws_skip_squote(const char *s, size_t len, size_t i, size_t *end);
int ws_skip_dquote(const char *s, size_t len, size_t i, size_t *end);
int ws_skip_backtick(const char *s, size_t len, size_t i, size_t *end);
int ws_skip_braced(const char *s, size_t len, size_t i, size_t *end);
int ws_skip_arith(const char *s, size_t len, size_t i, size_t *end);
int ws_skip_cmdsub(const char *s, size_t len, size_t i, size_t *end);
int ws_skip_dollar(const char *s, size_t len, size_t i, size_t *end);

#endif
