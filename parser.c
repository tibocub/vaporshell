/*
 * parser.c -- recursive-descent parser for the POSIX shell grammar
 * (Shell Command Language, section 2.10), producing the AST in ast.h.
 *
 * Reserved words are recognized here, by position, from unquoted T_WORD
 * tokens -- the lexer knows nothing about them. Bash extensions plug in
 * as extra cases in parse_command() / parse_simple(), gated by shell
 * options, rather than as a second parser.
 */

#include <nuttx/config.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "mode.h"
#include "parse.h"
#include "exec.h"

#define MAX_DEPTH 200

static struct node_s *parse_list(struct parser_s *p, bool multiline);
static struct node_s *parse_command(struct parser_s *p);
static struct node_s *parse_pipeline(struct parser_s *p);

/* ---- Token access and errors -------------------------------------------- */

static struct token_s *peek(struct parser_s *p)
{
  if (!p->have)
    {
      lex_token(&p->lx, &p->tok);
      p->have = true;

      if (p->tok.type == T_ERROR && p->err[0] == '\0')
        {
          snprintf(p->err, sizeof(p->err), "%s", p->lx.err);
          p->err_eof = p->lx.err_eof;
        }
    }

  return &p->tok;
}

static void advance(struct parser_s *p)
{
  p->have = false;
}

static const char *op_text(enum tok_e t)
{
  switch (t)
    {
      case T_ANDIF:     return "&&";
      case T_ORIF:      return "||";
      case T_DSEMI:     return ";;";
      case T_LESS:      return "<";
      case T_GREAT:     return ">";
      case T_DLESS:     return "<<";
      case T_DGREAT:    return ">>";
      case T_LESSAND:   return "<&";
      case T_GREATAND:  return ">&";
      case T_LESSGREAT: return "<>";
      case T_DLESSDASH: return "<<-";
      case T_CLOBBER:   return ">|";
      case T_SEMI:      return ";";
      case T_AMP:       return "&";
      case T_PIPE:      return "|";
      case T_LPAREN:    return "(";
      case T_RPAREN:    return ")";
      case T_ANDGREAT:  return "&>";
      case T_ANDDGREAT: return "&>>";
      case T_TLESS:     return "<<<";
      case T_SEMIAMP:   return ";&";
      case T_DSEMIAMP:  return ";;&";
      case T_NEWLINE:   return "newline";
      default:          return "?";
    }
}

/* Records the first syntax error, describing the offending token. */

static void unexpected(struct parser_s *p)
{
  struct token_s *t = peek(p);

  if (p->err[0] != '\0')
    {
      return;
    }

  if (t->type == T_EOF)
    {
      snprintf(p->err, sizeof(p->err), "syntax error: unexpected end of file");
      p->err_eof = true;
    }
  else
    {
      snprintf(p->err, sizeof(p->err),
               "syntax error near unexpected token `%s'",
               (t->type == T_WORD || t->type == T_IO_NUMBER) ? t->text
                                                              : op_text(t->type));
    }
}

static struct node_s *new_node(struct parser_s *p, enum node_type_e type)
{
  struct node_s *n = arena_alloc(p->lx.arena, sizeof(*n));

  n->type = type;
  return n;
}

static bool is_kw(struct parser_s *p, const char *kw)
{
  struct token_s *t = peek(p);

  return t->type == T_WORD && !t->quoted && strcmp(t->text, kw) == 0;
}

static bool expect_kw(struct parser_s *p, const char *kw)
{
  if (is_kw(p, kw))
    {
      advance(p);
      return true;
    }

  unexpected(p);
  return false;
}

static void skip_newlines(struct parser_s *p)
{
  while (peek(p)->type == T_NEWLINE)
    {
      advance(p);
    }
}

/* True at a token that closes a compound list. */

static bool at_list_end(struct parser_s *p)
{
  static const char *const closers[] =
  {
    "then", "else", "elif", "fi", "do", "done", "esac", "}"
  };
  struct token_s *t = peek(p);
  size_t i;

  if (t->type == T_EOF || t->type == T_ERROR || t->type == T_RPAREN ||
      t->type == T_DSEMI)
    {
      return true;
    }

  if (t->type != T_WORD || t->quoted)
    {
      return false;
    }

  for (i = 0; i < sizeof(closers) / sizeof(closers[0]); i++)
    {
      if (strcmp(t->text, closers[i]) == 0)
        {
          return true;
        }
    }

  return false;
}

/* POSIX: a function name is a name. Bash's default accepts almost any
 * word (foo-bar, a.b, ...) that is not an expansion or an assignment.
 */

static bool is_func_name(const char *s)
{
  size_t i;

  if (!vs_feat(VF_FUNC_NAME_ANY))
    {
      return is_valid_name(s, strlen(s));
    }

  if (s[0] == '\0')
    {
      return false;
    }

  for (i = 0; s[i] != '\0'; i++)
    {
      if (s[i] == '$' || s[i] == '`' || s[i] == '=')
        {
          return false;
        }
    }

  return true;
}

static bool is_assign_word(const char *text)
{
  return asg_is_word(text);
}

/* ---- Redirections -------------------------------------------------------- */

static bool is_redir_tok(enum tok_e t)
{
  return t == T_IO_NUMBER || t == T_LESS || t == T_GREAT || t == T_DLESS ||
         t == T_DGREAT || t == T_LESSAND || t == T_GREATAND ||
         t == T_LESSGREAT || t == T_DLESSDASH || t == T_CLOBBER ||
         t == T_ANDGREAT || t == T_ANDDGREAT || t == T_TLESS;
}

/* Heredoc delimiter: quote removal, and whether any quoting was present. */

static char *unquote_delim(struct parser_s *p, const char *raw, bool *quoted)
{
  struct sbuf_s out;
  const char *s = raw;

  sb_init(&out);
  *quoted = false;

  while (*s != '\0')
    {
      if (*s == '\\' && s[1] != '\0')
        {
          *quoted = true;
          sb_addc(&out, s[1]);
          s += 2;
        }
      else if (*s == '\'' || *s == '"')
        {
          char q = *s++;

          *quoted = true;
          while (*s != '\0' && *s != q)
            {
              if (q == '"' && *s == '\\' && s[1] != '\0' &&
                  strchr("$`\"\\", s[1]) != NULL)
                {
                  s++;
                }

              sb_addc(&out, *s++);
            }

          if (*s == q)
            {
              s++;
            }
        }
      else
        {
          sb_addc(&out, *s++);
        }
    }

  {
    char *result = arena_strdup(p->lx.arena, out.s != NULL ? out.s : "");

    sb_free(&out);
    return result;
  }
}

static bool parse_redir(struct parser_s *p, struct redir_s ***tail)
{
  struct redir_s *r = arena_alloc(p->lx.arena, sizeof(*r));
  struct token_s *t = peek(p);

  r->fd = -1;
  if (t->type == T_IO_NUMBER)
    {
      r->fd = atoi(t->text);
      advance(p);
      t = peek(p);
    }

  switch (t->type)
    {
      case T_LESS:      r->op = R_IN;      break;
      case T_GREAT:     r->op = R_OUT;     break;
      case T_CLOBBER:   r->op = R_CLOBBER; break;
      case T_DGREAT:    r->op = R_APPEND;  break;
      case T_LESSAND:   r->op = R_DUPIN;   break;
      case T_GREATAND:  r->op = R_DUPOUT;  break;
      case T_LESSGREAT: r->op = R_RDWR;    break;
      case T_ANDGREAT:  r->op = R_OUT_ERR;    break;
      case T_ANDDGREAT: r->op = R_APPEND_ERR; break;
      case T_TLESS:     r->op = R_HERESTR; break;
      case T_DLESS:
      case T_DLESSDASH:
        r->op = R_HEREDOC;
        r->hd_strip = (t->type == T_DLESSDASH);
        break;
      default:
        unexpected(p);
        return false;
    }

  advance(p);
  t = peek(p);
  if (t->type != T_WORD)
    {
      unexpected(p);
      return false;
    }

  if (r->op == R_HEREDOC)
    {
      r->hd_delim = unquote_delim(p, t->text, &r->hd_quoted);
      advance(p);
      lex_add_heredoc(&p->lx, r);   /* before anything can lex the newline */
    }
  else
    {
      r->target = t->text;
      advance(p);
    }

  **tail = r;
  *tail = &r->next;
  return true;
}

static bool parse_redir_list(struct parser_s *p, struct node_s *n)
{
  struct redir_s **tail = &n->redirs;

  while (is_redir_tok(peek(p)->type))
    {
      while (*tail != NULL)
        {
          tail = &(*tail)->next;
        }

      if (!parse_redir(p, &tail))
        {
          return false;
        }
    }

  return true;
}

/* ---- Simple commands and function definitions --------------------------- */

static struct word_s *new_word(struct parser_s *p, const char *text)
{
  struct word_s *w = arena_alloc(p->lx.arena, sizeof(*w));

  w->text = (char *)text;
  return w;
}

static struct node_s *parse_funcdef(struct parser_s *p, char *name)
{
  struct node_s *fn = new_node(p, N_FUNCDEF);
  struct node_s *body;

  advance(p);                     /* ( */
  if (peek(p)->type != T_RPAREN)
    {
      unexpected(p);
      return NULL;
    }

  advance(p);
  skip_newlines(p);
  body = parse_command(p);
  if (body == NULL)
    {
      return NULL;
    }

  if (body->type == N_SIMPLE || body->type == N_FUNCDEF)
    {
      snprintf(p->err, sizeof(p->err),
               "syntax error: function body must be a compound command");
      return NULL;
    }

  fn->name = name;
  fn->a = body;
  fn->arena = p->lx.arena;
  return fn;
}


/* ---- Alias expansion ------------------------------------------------------
 *
 * POSIX substitutes an alias by re-reading its text in place of the word,
 * which is exactly what this does: the token is cut out of the lexer buffer
 * and the alias text spliced in, then lexing resumes at the same offset. A
 * name is not expanded again while the parser is still inside its own text
 * (recursion), tracked by the buffer offset where each expansion ends.
 */

static bool is_reserved_word(const char *w)
{
  static const char *const words[] =
  {
    "if", "then", "else", "elif", "fi", "do", "done", "case", "esac",
    "while", "until", "for", "select", "in", "{", "}", "!", "[[", "]]", "function", "time", "coproc"
  };
  size_t i;

  for (i = 0; i < sizeof(words) / sizeof(words[0]); i++)
    {
      if (strcmp(w, words[i]) == 0)
        {
          return true;
        }
    }

  return false;
}

static bool try_alias(struct parser_s *p, struct token_s *t)
{
  struct lexer_s *lx = &p->lx;
  const struct alias_s *al;
  size_t start = t->start;
  size_t end = t->end;
  size_t vlen;
  size_t newlen;
  int i;
  int k;

  if (p->no_alias || t->type != T_WORD || t->quoted || g_sh.aliases == NULL ||
      !(g_sh.interactive || vs_feat(VF_ALIAS_SCRIPTS) || g_sh.so_expand_aliases) ||
      is_reserved_word(t->text))
    {
      return false;
    }

  al = alias_find(t->text);
  if (al == NULL)
    {
      return false;
    }

  /* Forget expansions we have left; refuse one we are inside. */

  for (i = 0, k = 0; i < p->naa; i++)
    {
      if (p->aa[i].end > start)
        {
          p->aa[k++] = p->aa[i];
        }
    }

  p->naa = k;
  for (i = 0; i < p->naa; i++)
    {
      if (strcmp(p->aa[i].name, t->text) == 0)
        {
          return false;
        }
    }

  vlen = strlen(al->value);
  newlen = lx->len - (end - start) + vlen;
  if (newlen + 1 > lx->cap)
    {
      lx->cap = (newlen + 1) * 2;
      lx->buf = vs_xrealloc(lx->buf, lx->cap);
    }

  memmove(lx->buf + start + vlen, lx->buf + end, lx->len - end);
  memcpy(lx->buf + start, al->value, vlen);
  lx->len = newlen;
  lx->pos = start;
  lx->lc_off = 0;
  lx->lc_line = 0;

  /* Enclosing expansions grow or shrink with the splice. */

  for (i = 0; i < p->naa; i++)
    {
      p->aa[i].end = p->aa[i].end - (end - start) + vlen;
    }

  if (p->naa < (int)(sizeof(p->aa) / sizeof(p->aa[0])))
    {
      p->aa[p->naa].name = al->name;
      p->aa[p->naa].end = start + vlen;
      p->naa++;
    }

  p->alias_blank = vlen > 0 && (al->value[vlen - 1] == ' ' || al->value[vlen - 1] == '\t');
  p->alias_blank_pos = start + vlen;
  p->have = false;
  return true;
}

static struct node_s *parse_simple(struct parser_s *p)
{
  struct node_s *n = new_node(p, N_SIMPLE);
  struct word_s **wtail = &n->words;
  bool first = true;
  struct word_s **atail = &n->assigns;
  struct redir_s **rtail = &n->redirs;
  bool seen_word = false;

  for (; ; )
    {
      struct token_s *t = peek(p);

      if (p->err[0] != '\0')
        {
          return NULL;
        }

      if (first)
        {
          n->line = t->line;
          first = false;
        }

      /* After an alias whose text ends in a blank the next word is looked
       * at for aliases too (POSIX 2.3.1).
       */

      if (t->type == T_WORD && seen_word && p->alias_blank &&
          t->start >= p->alias_blank_pos)
        {
          size_t at = t->start;

          p->alias_blank = false;
          if (try_alias(p, t))
            {
              if (!p->alias_blank)
                {
                  /* The replacement's own first word is also "the next
                   * word": x -> y -> `echo second` (dash does this).
                   */

                  p->alias_blank = true;
                  p->alias_blank_pos = at;
                }

              continue;
            }
        }

      if (is_redir_tok(t->type))
        {
          if (!parse_redir(p, &rtail))
            {
              return NULL;
            }

          continue;
        }

      if (t->type != T_WORD)
        {
          break;
        }

      if (!seen_word && is_assign_word(t->text))
        {
          *atail = new_word(p, t->text);
          atail = &(*atail)->next;
          advance(p);
          continue;
        }

      if (!seen_word && n->assigns == NULL && n->redirs == NULL &&
          !t->quoted && is_func_name(t->text))
        {
          char *name = t->text;

          advance(p);
          if (peek(p)->type == T_LPAREN)
            {
              return parse_funcdef(p, name);
            }

          *wtail = new_word(p, name);
          wtail = &(*wtail)->next;
          seen_word = true;
          continue;
        }

      *wtail = new_word(p, t->text);
      wtail = &(*wtail)->next;
      seen_word = true;
      advance(p);
    }

  if (n->words == NULL && n->assigns == NULL && n->redirs == NULL)
    {
      unexpected(p);
      return NULL;
    }

  return n;
}

/* ---- Compound commands --------------------------------------------------- */

static struct node_s *nonempty(struct parser_s *p, struct node_s *list)
{
  if (list != NULL && list->a == NULL)
    {
      unexpected(p);
      return NULL;
    }

  return list;
}

static struct node_s *parse_if_body(struct parser_s *p, struct node_s *n)
{
  n->a = nonempty(p, parse_list(p, true));
  if (n->a == NULL || !expect_kw(p, "then"))
    {
      return NULL;
    }

  n->b = nonempty(p, parse_list(p, true));
  if (n->b == NULL)
    {
      return NULL;
    }

  if (is_kw(p, "elif"))
    {
      advance(p);
      n->c = new_node(p, N_IF);
      return parse_if_body(p, n->c) != NULL ? n : NULL;
    }

  if (is_kw(p, "else"))
    {
      advance(p);
      n->c = nonempty(p, parse_list(p, true));
      if (n->c == NULL)
        {
          return NULL;
        }
    }

  return expect_kw(p, "fi") ? n : NULL;
}

static struct node_s *parse_while(struct parser_s *p, bool until)
{
  struct node_s *n = new_node(p, N_WHILE);

  advance(p);
  n->flag = until;
  n->a = nonempty(p, parse_list(p, true));
  if (n->a == NULL || !expect_kw(p, "do"))
    {
      return NULL;
    }

  n->b = nonempty(p, parse_list(p, true));
  if (n->b == NULL || !expect_kw(p, "done"))
    {
      return NULL;
    }

  return n;
}

/* ---- Bash syntax: (( )), for (( )), [[ ]], function, time ------------------- */

/* The text of an arithmetic command that starts at buffer offset 'start'
 * (just after "(("), up to the matching "))". It is scanned as raw text: `<`,
 * `;` and `&` inside would otherwise be lexed as shell operators. Returns
 * NULL if the parentheses do not close as "))" -- then it was `( (` nested
 * subshells after all.
 */

static char *scan_arith(struct parser_s *p, size_t start, size_t *after)
{
  struct lexer_s *lx = &p->lx;
  size_t i = start;
  int depth = 0;

  for (; ; )
    {
      while (i < lx->len)
        {
          char c = lx->buf[i];

          if (c == '\'' || c == '"')
            {
              size_t j = i + 1;

              while (j < lx->len && lx->buf[j] != c)
                {
                  j++;
                }

              i = j < lx->len ? j + 1 : lx->len;
              continue;
            }

          if (c == '(')
            {
              depth++;
            }
          else if (c == ')')
            {
              if (depth == 0)
                {
                  if (i + 1 >= lx->len && !lex_fetch(lx))
                    {
                      return NULL;
                    }

                  if (i + 1 < lx->len && lx->buf[i + 1] == ')')
                    {
                      *after = i + 2;
                      return arena_strndup(lx->arena, lx->buf + start, i - start);
                    }

                  return NULL;
                }

              depth--;
            }

          i++;
        }

      if (!lex_fetch(lx))
        {
          return NULL;
        }
    }
}

/* Moves the lexer past an arithmetic construct that scan_arith() found. */

static void skip_to(struct parser_s *p, size_t after)
{
  p->lx.pos = after;
  p->have = false;
}

/* for (( init; cond; step )) do ... done. 'start' is the offset after "((". */

static struct node_s *parse_arith_for(struct parser_s *p, size_t start)
{
  struct node_s *n = new_node(p, N_ARITHFOR);
  struct word_s **tail = &n->words;
  size_t after;
  char *text = scan_arith(p, start, &after);
  char *part;
  char *semi;
  int nparts = 0;
  int depth = 0;
  char *c;

  if (text == NULL)
    {
      snprintf(p->err, sizeof(p->err), "syntax error: bad for (( )) expression");
      return NULL;
    }

  skip_to(p, after);

  /* Split at the two top-level semicolons. */

  part = text;
  for (c = text; ; c++)
    {
      if (*c == '(')
        {
          depth++;
        }
      else if (*c == ')')
        {
          depth--;
        }

      if ((*c == ';' && depth == 0) || *c == '\0')
        {
          semi = c;
          *tail = new_word(p, arena_strndup(p->lx.arena, part, (size_t)(semi - part)));
          tail = &(*tail)->next;
          nparts++;
          if (*c == '\0')
            {
              break;
            }

          part = c + 1;
        }
    }

  if (nparts != 3)
    {
      snprintf(p->err, sizeof(p->err),
               "syntax error: for (( )) needs three expressions");
      return NULL;
    }

  if (peek(p)->type == T_SEMI)
    {
      advance(p);
    }

  skip_newlines(p);
  if (!expect_kw(p, "do"))
    {
      return NULL;
    }

  n->a = nonempty(p, parse_list(p, true));
  if (n->a == NULL || !expect_kw(p, "done"))
    {
      return NULL;
    }

  return n;
}

/* function name [()] compound-command */

static struct node_s *parse_function_kw(struct parser_s *p)
{
  struct node_s *fn = new_node(p, N_FUNCDEF);
  struct node_s *body;
  struct token_s *t;

  advance(p);                     /* function */
  t = peek(p);
  if (t->type != T_WORD || t->quoted)
    {
      unexpected(p);
      return NULL;
    }

  fn->name = t->text;
  advance(p);
  if (peek(p)->type == T_LPAREN)
    {
      advance(p);
      if (peek(p)->type != T_RPAREN)
        {
          unexpected(p);
          return NULL;
        }

      advance(p);
    }

  skip_newlines(p);
  body = parse_command(p);
  if (body == NULL)
    {
      return NULL;
    }

  if (body->type == N_SIMPLE || body->type == N_FUNCDEF)
    {
      snprintf(p->err, sizeof(p->err),
               "syntax error: function body must be a compound command");
      return NULL;
    }

  fn->a = body;
  fn->arena = p->lx.arena;
  return fn;
}

/* [[ expression ]]: the tokens up to the closing ]] are collected first so
 * the grammar can look ahead freely (`-f x` is a test but `-f == x` is a
 * string comparison).
 */

struct dbtok_s
{
  enum tok_e type;
  char *text;
  bool quoted;
};

struct dbctx_s
{
  struct parser_s *p;
  struct dbtok_s *t;
  int n;
  int i;
};

static const char *const g_db_unary[] =
{
  "-a", "-b", "-c", "-d", "-e", "-f", "-g", "-h", "-k", "-p", "-r", "-s", "-t",
  "-u", "-w", "-x", "-G", "-L", "-N", "-O", "-S", "-z", "-n", "-v", "-R", "-o",
  NULL
};

static const char *const g_db_binary[] =
{
  "==", "=", "!=", "=~", "-eq", "-ne", "-lt", "-le", "-gt", "-ge", "-nt", "-ot",
  "-ef", NULL
};

static bool db_in(const char *const *set, const char *s)
{
  for (; *set != NULL; set++)
    {
      if (strcmp(*set, s) == 0)
        {
          return true;
        }
    }

  return false;
}

static bool db_is_word(const struct dbctx_s *c, int i)
{
  return i < c->n && c->t[i].type == T_WORD;
}

static const char *db_binop(const struct dbctx_s *c, int i)
{
  if (i >= c->n)
    {
      return NULL;
    }

  if (c->t[i].type == T_LESS)
    {
      return "<";
    }

  if (c->t[i].type == T_GREAT)
    {
      return ">";
    }

  if (c->t[i].type == T_WORD && !c->t[i].quoted && db_in(g_db_binary, c->t[i].text))
    {
      return c->t[i].text;
    }

  return NULL;
}

static struct node_s *db_or(struct dbctx_s *c);

static struct node_s *db_error(struct dbctx_s *c)
{
  if (c->p->err[0] == '\0')
    {
      snprintf(c->p->err, sizeof(c->p->err),
               "syntax error in conditional expression");
    }

  return NULL;
}

static struct node_s *db_primary(struct dbctx_s *c)
{
  struct parser_s *p = c->p;

  if (c->i >= c->n)
    {
      return db_error(c);
    }

  if (c->t[c->i].type == T_LPAREN)
    {
      struct node_s *e;

      c->i++;
      e = db_or(c);
      if (e == NULL || c->i >= c->n || c->t[c->i].type != T_RPAREN)
        {
          return db_error(c);
        }

      c->i++;
      return e;
    }

  if (db_is_word(c, c->i) && !c->t[c->i].quoted && strcmp(c->t[c->i].text, "!") == 0)
    {
      struct node_s *n = new_node(p, N_DB_NOT);

      c->i++;
      n->a = db_primary(c);
      return n->a != NULL ? n : NULL;
    }

  if (!db_is_word(c, c->i))
    {
      return db_error(c);
    }

  /* unary operator: an operator word followed by a word that is not itself
   * the start of a binary comparison
   */

  if (!c->t[c->i].quoted && db_in(g_db_unary, c->t[c->i].text) &&
      db_is_word(c, c->i + 1) && db_binop(c, c->i + 1) == NULL)
    {
      struct node_s *n = new_node(p, N_DB_TEST);

      n->name = c->t[c->i].text;
      n->words = new_word(p, c->t[c->i + 1].text);
      c->i += 2;
      return n;
    }

  if (db_binop(c, c->i + 1) != NULL)
    {
      struct node_s *n = new_node(p, N_DB_TEST);
      struct word_s *l = new_word(p, c->t[c->i].text);

      n->name = (char *)db_binop(c, c->i + 1);
      if (!db_is_word(c, c->i + 2))
        {
          return db_error(c);
        }

      n->words = l;
      l->next = new_word(p, c->t[c->i + 2].text);
      c->i += 3;
      return n;
    }

  /* a unary operator with nothing to operate on is an error in bash */

  if (!c->t[c->i].quoted && db_in(g_db_unary, c->t[c->i].text))
    {
      snprintf(p->err, sizeof(p->err),
               "syntax error: unexpected argument to conditional unary operator");
      return NULL;
    }

  {
    struct node_s *n = new_node(p, N_DB_TEST);

    n->name = arena_strdup(p->lx.arena, "");
    n->words = new_word(p, c->t[c->i].text);
    c->i++;
    return n;
  }
}

static struct node_s *db_and(struct dbctx_s *c)
{
  struct node_s *l = db_primary(c);

  while (l != NULL && c->i < c->n && c->t[c->i].type == T_ANDIF)
    {
      struct node_s *n = new_node(c->p, N_DB_AND);

      c->i++;
      n->a = l;
      n->b = db_primary(c);
      if (n->b == NULL)
        {
          return NULL;
        }

      l = n;
    }

  return l;
}

static struct node_s *db_or(struct dbctx_s *c)
{
  struct node_s *l = db_and(c);

  while (l != NULL && c->i < c->n && c->t[c->i].type == T_ORIF)
    {
      struct node_s *n = new_node(c->p, N_DB_OR);

      c->i++;
      n->a = l;
      n->b = db_and(c);
      if (n->b == NULL)
        {
          return NULL;
        }

      l = n;
    }

  return l;
}

/* The right side of =~: a regular expression is read as one word. Parentheses
 * nest (so a group may contain spaces) and quotes and $(...) are honoured; it
 * ends at the first blank outside them. Returns NULL if there is nothing.
 */

static char *scan_regex_word(struct parser_s *p)
{
  struct lexer_s *lx = &p->lx;
  size_t i = lx->pos;
  size_t start;
  int depth = 0;

  while (i < lx->len && (lx->buf[i] == ' ' || lx->buf[i] == '\t'))
    {
      i++;
    }

  start = i;
  while (i < lx->len)
    {
      char c = lx->buf[i];
      size_t e;

      if (c == '\\' && i + 1 < lx->len)
        {
          i += 2;
          continue;
        }

      if ((c == '\'' && ws_skip_squote(lx->buf, lx->len, i, &e) == WS_OK) ||
          (c == '"' && ws_skip_dquote(lx->buf, lx->len, i, &e) == WS_OK) ||
          (c == '`' && ws_skip_backtick(lx->buf, lx->len, i, &e) == WS_OK) ||
          (c == '$' && i + 1 < lx->len && (lx->buf[i + 1] == '(' || lx->buf[i + 1] == '{') &&
           ws_skip_dollar(lx->buf, lx->len, i, &e) == WS_OK))
        {
          i = e;
          continue;
        }

      if (c == '(')
        {
          depth++;
        }
      else if (c == ')' && depth > 0)
        {
          depth--;
        }
      else if (depth == 0 && (c == ' ' || c == '\t' || c == '\n'))
        {
          break;
        }

      i++;
    }

  if (i == start)
    {
      return NULL;
    }

  lx->pos = i;
  return arena_strndup(lx->arena, lx->buf + start, i - start);
}

static struct node_s *parse_dbracket(struct parser_s *p)
{
  struct node_s *n = new_node(p, N_DBRACKET);
  struct dbctx_s c;
  int cap = 16;

  c.p = p;
  c.n = 0;
  c.i = 0;
  c.t = vs_xmalloc((size_t)cap * sizeof(*c.t));
  p->lx.force_extglob = true;     /* patterns like +(a|b) are words in here */
  advance(p);                     /* [[ */

  for (; ; )
    {
      struct token_s *t;

      if (c.n > 0 && !p->have && c.t[c.n - 1].type == T_WORD && !c.t[c.n - 1].quoted &&
          strcmp(c.t[c.n - 1].text, "=~") == 0)
        {
          char *rx = scan_regex_word(p);

          if (rx != NULL)
            {
              if (c.n == cap)
                {
                  cap *= 2;
                  c.t = vs_xrealloc(c.t, (size_t)cap * sizeof(*c.t));
                }

              c.t[c.n].type = T_WORD;
              c.t[c.n].quoted = strpbrk(rx, "'\"\\") != NULL;
              c.t[c.n].text = rx;
              c.n++;
              continue;
            }
        }

      t = peek(p);

      if (t->type == T_EOF || t->type == T_ERROR)
        {
          p->lx.force_extglob = false;
          unexpected(p);
          free(c.t);
          return NULL;
        }

      if (t->type == T_NEWLINE)
        {
          advance(p);
          continue;
        }

      if (t->type == T_WORD && !t->quoted && strcmp(t->text, "]]") == 0)
        {
          p->lx.force_extglob = false;
          advance(p);
          break;
        }

      if (c.n == cap)
        {
          cap *= 2;
          c.t = vs_xrealloc(c.t, (size_t)cap * sizeof(*c.t));
        }

      c.t[c.n].type = t->type;
      c.t[c.n].quoted = t->quoted;
      c.t[c.n].text = (t->type == T_WORD) ? t->text : NULL;
      c.n++;
      advance(p);
    }

  n->a = db_or(&c);
  if (n->a != NULL && c.i != c.n)
    {
      db_error(&c);
      n->a = NULL;
    }

  free(c.t);
  return n->a != NULL ? n : NULL;
}

static struct node_s *parse_for(struct parser_s *p)
{
  struct node_s *n = new_node(p, N_FOR);
  struct token_s *t;

  advance(p);
  t = peek(p);

  if (t->type == T_LPAREN && vs_feat(VF_BASH_SYNTAX) && t->end < p->lx.len &&
      p->lx.buf[t->end] == '(')
    {
      return parse_arith_for(p, t->end + 1);
    }

  if (t->type != T_WORD || t->quoted || !is_valid_name(t->text, strlen(t->text)))
    {
      unexpected(p);
      return NULL;
    }

  n->name = t->text;
  advance(p);
  skip_newlines(p);

  if (is_kw(p, "in"))
    {
      struct word_s **tail = &n->words;

      advance(p);
      n->flag = true;
      while (peek(p)->type == T_WORD)
        {
          *tail = new_word(p, peek(p)->text);
          tail = &(*tail)->next;
          advance(p);
        }

      if (peek(p)->type != T_SEMI && peek(p)->type != T_NEWLINE)
        {
          unexpected(p);
          return NULL;
        }

      advance(p);
    }
  else if (peek(p)->type == T_SEMI)
    {
      advance(p);
    }

  skip_newlines(p);
  if (!expect_kw(p, "do"))
    {
      return NULL;
    }

  n->a = nonempty(p, parse_list(p, true));
  if (n->a == NULL || !expect_kw(p, "done"))
    {
      return NULL;
    }

  return n;
}

/* Is 't' the start of a compound command (bash's grammar: coproc's NAME is
 * only recognised when followed by one, never a plain simple command)?
 */

static bool starts_compound(const struct token_s *t)
{
  static const char *const kw[] = { "{", "if", "while", "until", "for",
                                    "select", "case", "[[", NULL };
  int i;

  if (t->type == T_LPAREN)
    {
      return true;                 /* a subshell, or the start of (( */
    }

  if (t->type != T_WORD || t->quoted)
    {
      return false;
    }

  for (i = 0; kw[i] != NULL; i++)
    {
      if (strcmp(t->text, kw[i]) == 0)
        {
          return true;
        }
    }

  return false;
}

/* coproc [NAME] command -- NAME (default "COPROC") only exists as a
 * distinct word when a compound command follows it; `coproc mycp cat` is
 * the plain two-word simple command `mycp cat`, unnamed, exactly as in
 * bash (its own grammar has no "coproc NAME simple-command" rule either).
 */

static struct node_s *parse_coproc(struct parser_s *p)
{
  struct node_s *n = new_node(p, N_COPROC);
  struct token_s *t;

  advance(p);                      /* the "coproc" keyword */
  n->name = "COPROC";
  t = peek(p);
  if (t->type == T_WORD && !t->quoted && is_valid_name(t->text, strlen(t->text)))
    {
      size_t save_pos = p->lx.pos;
      struct token_s save_tok = p->tok;
      bool save_have = p->have;
      char *name = t->text;

      advance(p);
      if (starts_compound(peek(p)))
        {
          n->name = name;
        }
      else
        {
          p->lx.pos = save_pos;
          p->tok = save_tok;
          p->have = save_have;
        }
    }

  n->a = parse_command(p);
  if (n->a == NULL)
    {
      return NULL;
    }

  return n;
}

/* select NAME [in WORDS] ; do LIST done  -- same shape as `for`, minus the
 * `for ((..))` arithmetic form, which `select` has no equivalent of.
 */

static struct node_s *parse_select(struct parser_s *p)
{
  struct node_s *n = new_node(p, N_SELECT);
  struct token_s *t;

  advance(p);
  t = peek(p);

  if (t->type != T_WORD || t->quoted || !is_valid_name(t->text, strlen(t->text)))
    {
      unexpected(p);
      return NULL;
    }

  n->name = t->text;
  advance(p);
  skip_newlines(p);

  if (is_kw(p, "in"))
    {
      struct word_s **tail = &n->words;

      advance(p);
      n->flag = true;
      while (peek(p)->type == T_WORD)
        {
          *tail = new_word(p, peek(p)->text);
          tail = &(*tail)->next;
          advance(p);
        }

      if (peek(p)->type != T_SEMI && peek(p)->type != T_NEWLINE)
        {
          unexpected(p);
          return NULL;
        }

      advance(p);
    }
  else if (peek(p)->type == T_SEMI)
    {
      advance(p);
    }

  skip_newlines(p);
  if (!expect_kw(p, "do"))
    {
      return NULL;
    }

  n->a = nonempty(p, parse_list(p, true));
  if (n->a == NULL || !expect_kw(p, "done"))
    {
      return NULL;
    }

  return n;
}

static struct node_s *parse_case(struct parser_s *p)
{
  struct node_s *n = new_node(p, N_CASE);
  struct case_item_s **itail = &n->items;
  struct token_s *t;

  advance(p);
  t = peek(p);
  if (t->type != T_WORD)
    {
      unexpected(p);
      return NULL;
    }

  n->words = new_word(p, t->text);
  advance(p);
  skip_newlines(p);
  if (!expect_kw(p, "in"))
    {
      return NULL;
    }

  skip_newlines(p);

  while (!is_kw(p, "esac"))
    {
      struct case_item_s *item = arena_alloc(p->lx.arena, sizeof(*item));
      struct word_s **ptail = &item->patterns;

      if (p->err[0] != '\0')
        {
          return NULL;
        }

      if (peek(p)->type == T_LPAREN)
        {
          advance(p);
        }

      for (; ; )
        {
          t = peek(p);
          if (t->type != T_WORD)
            {
              unexpected(p);
              return NULL;
            }

          *ptail = new_word(p, t->text);
          ptail = &(*ptail)->next;
          advance(p);

          if (peek(p)->type != T_PIPE)
            {
              break;
            }

          advance(p);
        }

      if (peek(p)->type != T_RPAREN)
        {
          unexpected(p);
          return NULL;
        }

      advance(p);
      item->body = parse_list(p, true);
      if (item->body == NULL)
        {
          return NULL;
        }

      *itail = item;
      itail = &item->next;

      if (peek(p)->type == T_DSEMI)
        {
          advance(p);
          skip_newlines(p);
        }
      else if (peek(p)->type == T_SEMIAMP || peek(p)->type == T_DSEMIAMP)
        {
          item->term = peek(p)->type == T_SEMIAMP ? 1 : 2;   /* bash: ;& and ;;& */
          advance(p);
          skip_newlines(p);
        }
      else if (!is_kw(p, "esac"))
        {
          unexpected(p);
          return NULL;
        }
    }

  advance(p);                     /* esac */
  return n;
}

static struct node_s *parse_command(struct parser_s *p)
{
  struct node_s *n = NULL;
  struct token_s *t = peek(p);

  while (t->type == T_WORD && try_alias(p, t))
    {
      t = peek(p);
    }

  if (++p->depth > MAX_DEPTH)
    {
      snprintf(p->err, sizeof(p->err), "syntax error: nesting too deep");
      return NULL;
    }

  if (t->type == T_LPAREN && vs_feat(VF_BASH_SYNTAX) && t->end < p->lx.len &&
      p->lx.buf[t->end] == '(')
    {
      size_t after;
      char *text = scan_arith(p, t->end + 1, &after);

      if (text != NULL)
        {
          n = new_node(p, N_ARITH);
          n->words = new_word(p, text);
          skip_to(p, after);
          t = peek(p);
        }
    }

  if (n != NULL)
    {
      /* an arithmetic command: nothing more to parse */
    }
  else if (t->type == T_LPAREN)
    {
      n = new_node(p, N_SUBSHELL);
      advance(p);
      n->a = nonempty(p, parse_list(p, true));
      if (n->a == NULL)
        {
          return NULL;
        }

      if (peek(p)->type != T_RPAREN)
        {
          unexpected(p);
          return NULL;
        }

      advance(p);
    }
  else if (t->type == T_WORD && !t->quoted)
    {
      if (strcmp(t->text, "{") == 0)
        {
          n = new_node(p, N_BRACE);
          advance(p);
          n->a = nonempty(p, parse_list(p, true));
          if (n->a == NULL || !expect_kw(p, "}"))
            {
              return NULL;
            }
        }
      else if (strcmp(t->text, "if") == 0)
        {
          n = new_node(p, N_IF);
          advance(p);
          n = parse_if_body(p, n);
        }
      else if (strcmp(t->text, "while") == 0)
        {
          n = parse_while(p, false);
        }
      else if (strcmp(t->text, "until") == 0)
        {
          n = parse_while(p, true);
        }
      else if (strcmp(t->text, "for") == 0)
        {
          n = parse_for(p);
        }
      else if (strcmp(t->text, "select") == 0 && vs_feat(VF_BASH_SYNTAX))
        {
          n = parse_select(p);
        }
      else if (strcmp(t->text, "coproc") == 0 && vs_feat(VF_BASH_SYNTAX))
        {
          n = parse_coproc(p);
        }
      else if (strcmp(t->text, "case") == 0)
        {
          n = parse_case(p);
        }
      else if (strcmp(t->text, "[[") == 0 && vs_feat(VF_BASH_SYNTAX))
        {
          n = parse_dbracket(p);
        }
      else if (strcmp(t->text, "function") == 0 && vs_feat(VF_BASH_SYNTAX))
        {
          n = parse_function_kw(p);
          if (n != NULL)
            {
              p->depth--;
              return n;
            }
        }
      else if (at_list_end(p))
        {
          unexpected(p);            /* stray then/fi/done/... */
          return NULL;
        }
    }

  if (n == NULL && p->err[0] == '\0')
    {
      n = parse_simple(p);
      p->depth--;
      return n;
    }

  p->depth--;
  if (n != NULL && n->type != N_FUNCDEF && n->type != N_SIMPLE &&
      !parse_redir_list(p, n))
    {
      return NULL;
    }

  return n;
}

/* ---- Pipelines, and-or lists, lists ------------------------------------- */

static struct node_s *parse_pipeline(struct parser_s *p)
{
  bool bang = false;
  struct node_s *first;
  struct node_s *pipe;
  struct node_s **tail;

  if (is_kw(p, "time") && vs_feat(VF_BASH_SYNTAX))
    {
      struct node_s *tn = new_node(p, N_TIME);

      advance(p);
      if (is_kw(p, "-p"))
        {
          tn->flag = true;
          advance(p);
        }

      tn->a = parse_pipeline(p);
      return tn->a != NULL ? tn : NULL;
    }

  if (is_kw(p, "!"))
    {
      bang = true;
      advance(p);
    }

  first = parse_command(p);
  if (first == NULL)
    {
      return NULL;
    }

  if (!bang && peek(p)->type != T_PIPE)
    {
      return first;
    }

  pipe = new_node(p, N_PIPE);
  pipe->flag = bang;
  pipe->a = first;
  tail = &first->next;

  while (peek(p)->type == T_PIPE)
    {
      struct node_s *stage;

      advance(p);
      skip_newlines(p);
      stage = parse_command(p);
      if (stage == NULL)
        {
          return NULL;
        }

      *tail = stage;
      tail = &stage->next;
    }

  return pipe;
}

static struct node_s *parse_and_or(struct parser_s *p)
{
  struct node_s *left = parse_pipeline(p);

  while (left != NULL &&
         (peek(p)->type == T_ANDIF || peek(p)->type == T_ORIF))
    {
      struct node_s *n = new_node(p, peek(p)->type == T_ANDIF ? N_AND : N_OR);

      advance(p);
      skip_newlines(p);
      n->a = left;
      n->b = parse_pipeline(p);
      if (n->b == NULL)
        {
          return NULL;
        }

      left = n;
    }

  return left;
}

/* multiline: a compound list (newlines separate commands, stops at a
 * closing keyword); otherwise one command line (stops at the newline).
 */

static struct node_s *parse_list(struct parser_s *p, bool multiline)
{
  struct node_s *list = new_node(p, N_LIST);
  struct node_s **tail = &list->a;

  if (multiline)
    {
      skip_newlines(p);
    }

  for (; ; )
    {
      struct node_s *cmd;
      struct token_s *t;

      if (p->err[0] != '\0')
        {
          return NULL;
        }

      t = peek(p);
      if ((!multiline && t->type == T_NEWLINE) || at_list_end(p))
        {
          break;
        }

      cmd = parse_and_or(p);
      if (cmd == NULL)
        {
          return NULL;
        }

      *tail = cmd;
      tail = &cmd->next;

      t = peek(p);
      if (t->type == T_SEMI || t->type == T_AMP)
        {
          cmd->async = (t->type == T_AMP);
          advance(p);
        }
      else if (!(multiline && t->type == T_NEWLINE))
        {
          break;
        }

      if (multiline)
        {
          skip_newlines(p);
        }
    }

  return list;
}

/* ---- Entry points -------------------------------------------------------- */

void parser_init(struct parser_s *p, vs_line_fn fn, void *ctx)
{
  memset(p, 0, sizeof(*p));
  lexer_init(&p->lx, fn, ctx);
}

void parser_init_mem(struct parser_s *p, const char *text, size_t len)
{
  parser_init(p, NULL, NULL);
  p->lx.buf = vs_xmalloc(len + 1);
  memcpy(p->lx.buf, text, len);
  p->lx.len = p->lx.cap = len;
}

void parser_free(struct parser_s *p)
{
  lexer_free(&p->lx);
}

void parser_discard(struct parser_s *p)
{
  p->lx.pos = p->lx.len;
  p->lx.hd_head = NULL;
  p->have = false;
}

enum parse_result_e parse_command_line(struct parser_s *p,
                                       struct arena_s **arena,
                                       struct node_s **node)
{
  struct token_s *t;
  struct node_s *list;

  *arena = arena_new();
  *node = NULL;
  p->lx.arena = *arena;
  p->err[0] = p->lx.err[0] = '\0';
  p->err_eof = p->lx.err_eof = false;
  p->depth = 0;
  p->naa = 0;
  p->alias_blank = false;

  if (!p->have)
    {
      p->lx.fresh = (p->lx.pos >= p->lx.len);
      if (p->lx.fresh)
        {
          size_t k;

          for (k = 0; k < p->lx.len; k++)
            {
              if (p->lx.buf[k] == '\n')
                {
                  p->lx.line_base++;
                }
            }

          p->lx.pos = p->lx.len = 0;
          p->lx.lc_off = 0;
          p->lx.lc_line = 0;
        }
    }

  t = peek(p);
  if (t->type == T_EOF)
    {
      return PARSE_EOF;
    }

  if (t->type == T_NEWLINE)
    {
      advance(p);
      return PARSE_OK;
    }

  list = parse_list(p, false);
  if (list == NULL || p->err[0] != '\0')
    {
      if (p->err[0] == '\0')
        {
          unexpected(p);
        }

      return PARSE_ERR;
    }

  t = peek(p);
  if (t->type == T_NEWLINE)
    {
      advance(p);
    }
  else if (t->type != T_EOF)
    {
      unexpected(p);
      return PARSE_ERR;
    }

  *node = list;
  return PARSE_OK;
}

/* Called by the scanner for "$(": parse a command list on the text and
 * report where its closing ')' is. The AST is thrown away -- the expander
 * parses again when it runs the substitution.
 */

int ws_skip_cmdsub(const char *s, size_t len, size_t i, size_t *end)
{
  struct parser_s sub;
  struct arena_s *arena = arena_new();
  int r;

  parser_init_mem(&sub, s + i, len - i);
  sub.lx.arena = arena;
  sub.lx.fresh = false;
  sub.no_alias = true;          /* the text is only being measured */

  parse_list(&sub, true);
  if (sub.err[0] != '\0')
    {
      r = sub.err_eof ? WS_INCOMPLETE : WS_ERROR;
    }
  else if (peek(&sub)->type == T_RPAREN)
    {
      *end = i + sub.lx.pos;
      r = WS_OK;
    }
  else
    {
      r = (peek(&sub)->type == T_EOF) ? WS_INCOMPLETE : WS_ERROR;
    }

  parser_free(&sub);
  arena_release(arena);
  return r;
}

/* <(...) and >(...): 'i' is the index of the '<' or '>'; the command list
 * inside is exactly like $(...)'s, so this is only an offset adjustment.
 */

int ws_skip_procsub(const char *s, size_t len, size_t i, size_t *end)
{
  return ws_skip_cmdsub(s, len, i + 2, end);
}
