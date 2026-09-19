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
#include "parse.h"

#define MAX_DEPTH 200

static struct node_s *parse_list(struct parser_s *p, bool multiline);
static struct node_s *parse_command(struct parser_s *p);

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

static bool name_start(char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool name_char(char c)
{
  return name_start(c) || (c >= '0' && c <= '9');
}

static bool is_assign_word(const char *text)
{
  size_t i = 0;

  if (!name_start(text[0]))
    {
      return false;
    }

  while (name_char(text[i]))
    {
      i++;
    }

  return text[i] == '=';
}

/* ---- Redirections -------------------------------------------------------- */

static bool is_redir_tok(enum tok_e t)
{
  return t == T_IO_NUMBER || t == T_LESS || t == T_GREAT || t == T_DLESS ||
         t == T_DGREAT || t == T_LESSAND || t == T_GREATAND ||
         t == T_LESSGREAT || t == T_DLESSDASH || t == T_CLOBBER;
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

static struct node_s *parse_simple(struct parser_s *p)
{
  struct node_s *n = new_node(p, N_SIMPLE);
  struct word_s **wtail = &n->words;
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
          !t->quoted && is_valid_name(t->text, strlen(t->text)))
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

static struct node_s *parse_for(struct parser_s *p)
{
  struct node_s *n = new_node(p, N_FOR);
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

  if (++p->depth > MAX_DEPTH)
    {
      snprintf(p->err, sizeof(p->err), "syntax error: nesting too deep");
      return NULL;
    }

  if (t->type == T_LPAREN)
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
      else if (strcmp(t->text, "case") == 0)
        {
          n = parse_case(p);
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

  if (!p->have)
    {
      p->lx.fresh = (p->lx.pos >= p->lx.len);
      if (p->lx.fresh)
        {
          p->lx.pos = p->lx.len = 0;
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
