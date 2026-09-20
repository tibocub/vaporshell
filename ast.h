/*
 * ast.h -- the parsed form of a command line.
 *
 * Words stay as *raw source text* (quotes and $-constructs intact). The
 * expander is the one place that interprets them, so quoting is never
 * decided anywhere else. Everything is allocated from one arena per
 * parsed command line and freed together.
 */

#ifndef VAPORSHELL_AST_H
#define VAPORSHELL_AST_H

#include <stdbool.h>
#include <stddef.h>

/* ---- Arena (reference counted: a function body outlives its parse) ------ */

struct arena_s *arena_new(void);
void *arena_alloc(struct arena_s *a, size_t n);
char *arena_strdup(struct arena_s *a, const char *s);
char *arena_strndup(struct arena_s *a, const char *s, size_t n);
void arena_retain(struct arena_s *a);
void arena_release(struct arena_s *a);

/* ---- Nodes -------------------------------------------------------------- */

struct word_s
{
  struct word_s *next;
  char *text;                 /* raw source text */
};

enum redir_op_e
{
  R_IN,                       /* <  */
  R_OUT,                      /* >  */
  R_CLOBBER,                  /* >| */
  R_APPEND,                   /* >> */
  R_DUPIN,                    /* <& */
  R_DUPOUT,                   /* >& */
  R_RDWR,                     /* <> */
  R_HEREDOC,                  /* << and <<- */
  R_OUT_ERR,                  /* &>  (bash: stdout and stderr) */
  R_APPEND_ERR,               /* &>> */
  R_HERESTR                   /* <<< (bash): target is the word */
};

struct redir_s
{
  struct redir_s *next;
  int fd;                     /* -1: the operator's default */
  enum redir_op_e op;
  char *target;               /* raw word (not used by R_HEREDOC) */
  char *hd_delim;             /* R_HEREDOC: delimiter, quotes removed */
  char *hd_body;              /* R_HEREDOC: filled in by the lexer */
  bool hd_quoted;             /* delimiter was quoted: body is literal */
  bool hd_strip;              /* <<-: leading tabs stripped */
};

enum node_type_e
{
  N_SIMPLE,                   /* assigns, words, redirs */
  N_PIPE,                     /* a: first stage (chained by ->next); flag: negated */
  N_AND,                      /* a && b */
  N_OR,                       /* a || b */
  N_LIST,                     /* a: items chained by ->next (each has ->async) */
  N_SUBSHELL,                 /* a: body, redirs */
  N_BRACE,                    /* a: body, redirs */
  N_IF,                       /* a: condition, b: then, c: else (may be an N_IF) */
  N_WHILE,                    /* a: condition, b: body, flag: until */
  N_FOR,                      /* name, words, flag: has "in", a: body */
  N_CASE,                     /* words: subject (single), items */
  N_FUNCDEF,                  /* name, a: body */
  N_DBRACKET,                 /* [[ ]]: a: the expression */
  N_DB_AND,                   /*   a && b */
  N_DB_OR,                    /*   a || b */
  N_DB_NOT,                   /*   ! a */
  N_DB_TEST,                  /*   name: operator ("" = string test), words: operands */
  N_ARITH,                    /* (( )): words: the expression */
  N_ARITHFOR,                 /* for (( )): words: init, cond, step; a: body */
  N_TIME                      /* time [-p] pipeline: a: the pipeline; flag: -p */
};

struct case_item_s
{
  struct case_item_s *next;
  struct word_s *patterns;
  struct node_s *body;        /* an N_LIST, possibly empty */
  int term;                   /* 0: ;;  1: ;& (fall through)  2: ;;& (keep testing) */
};

struct node_s
{
  enum node_type_e type;
  struct node_s *next;        /* next stage / next list item */
  struct node_s *a;
  struct node_s *b;
  struct node_s *c;
  struct word_s *words;
  struct word_s *assigns;     /* N_SIMPLE: raw "name=value" words */
  struct redir_s *redirs;
  struct case_item_s *items;
  char *name;
  bool flag;
  bool async;                 /* list item ended by '&' */
  int line;                   /* N_SIMPLE: source line, for $LINENO */
  struct arena_s *arena;      /* N_FUNCDEF: arena the body lives in */
};

#endif
