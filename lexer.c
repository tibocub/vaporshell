/*
 * lexer.c -- turns input text into tokens (POSIX Shell Command Language,
 * section 2.3). A word is kept as raw source text; it ends at an unquoted
 * metacharacter. Quotes, ${}, $() and backticks are skipped as units (via
 * wordscan.c) so metacharacters inside them never split the word.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "parse.h"

static const struct
{
  const char *text;
  enum tok_e type;
} g_ops[] =
{
  { "<<-", T_DLESSDASH }, { "&&", T_ANDIF },   { "||", T_ORIF },
  { ";;", T_DSEMI },      { "<<", T_DLESS },   { ">>", T_DGREAT },
  { "<&", T_LESSAND },    { ">&", T_GREATAND }, { "<>", T_LESSGREAT },
  { ">|", T_CLOBBER },    { "<", T_LESS },     { ">", T_GREAT },
  { ";", T_SEMI },        { "&", T_AMP },      { "|", T_PIPE },
  { "(", T_LPAREN },      { ")", T_RPAREN }
};

void lexer_init(struct lexer_s *lx, vs_line_fn fn, void *ctx)
{
  memset(lx, 0, sizeof(*lx));
  lx->getline = fn;
  lx->ctx = ctx;
}

void lexer_free(struct lexer_s *lx)
{
  free(lx->buf);
  lx->buf = NULL;
  lx->len = lx->cap = lx->pos = 0;
}

static void lex_error(struct lexer_s *lx, bool eof, const char *msg)
{
  if (lx->err[0] == '\0')
    {
      snprintf(lx->err, sizeof(lx->err), "%s", msg);
      lx->err_eof = eof;
    }
}

/* Appends the next input line to the buffer. Indices into the buffer stay
 * valid; pointers do not.
 */

static bool lex_more(struct lexer_s *lx)
{
  char *line;
  size_t n;

  if (lx->getline == NULL)
    {
      return false;
    }

  line = lx->getline(lx->ctx, !lx->fresh);
  lx->fresh = false;
  if (line == NULL)
    {
      return false;
    }

  n = strlen(line);
  if (lx->len + n + 1 > lx->cap)
    {
      lx->cap = (lx->len + n + 1) * 2;
      lx->buf = vs_xrealloc(lx->buf, lx->cap);
    }

  memcpy(lx->buf + lx->len, line, n);
  lx->len += n;
  free(line);
  return true;
}

static bool is_meta(char c)
{
  return c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '&' ||
         c == '|' || c == '(' || c == ')' || c == '<' || c == '>';
}

/* ---- Here-documents ----------------------------------------------------- */

void lex_add_heredoc(struct lexer_s *lx, struct redir_s *r)
{
  struct hd_pending_s *node = arena_alloc(lx->arena, sizeof(*node));
  struct hd_pending_s **slot = &lx->hd_head;

  node->redir = r;
  while (*slot != NULL)
    {
      slot = &(*slot)->next;
    }

  *slot = node;
}

/* Returns the next full line in the buffer (fetching more if needed) as
 * [*start, *end) without its newline; false at end of input.
 */

static bool lex_line(struct lexer_s *lx, size_t *start, size_t *end)
{
  size_t i;

  for (;;)
    {
      for (i = lx->pos; i < lx->len && lx->buf[i] != '\n'; i++)
        {
        }

      if (i < lx->len)
        {
          *start = lx->pos;
          *end = i;
          lx->pos = i + 1;
          return true;
        }

      if (!lex_more(lx))
        {
          if (lx->pos < lx->len)
            {
              *start = lx->pos;
              *end = lx->len;
              lx->pos = lx->len;
              return true;
            }

          return false;
        }
    }
}

/* Called right after a newline is consumed: read every pending body. */

static void lex_read_heredocs(struct lexer_s *lx)
{
  while (lx->hd_head != NULL)
    {
      struct redir_s *r = lx->hd_head->redir;
      struct sbuf_s body;
      size_t start;
      size_t end;

      lx->hd_head = lx->hd_head->next;
      sb_init(&body);

      while (lex_line(lx, &start, &end))
        {
          const char *line = lx->buf + start;
          size_t n = end - start;

          if (r->hd_strip)
            {
              while (n > 0 && *line == '\t')
                {
                  line++;
                  n--;
                }
            }

          if (n == strlen(r->hd_delim) && memcmp(line, r->hd_delim, n) == 0)
            {
              goto done;
            }

          sb_addn(&body, line, n);
          sb_addc(&body, '\n');
        }

      vs_err("warning: here-document delimited by end-of-file "
             "(wanted `%s')", r->hd_delim);

done:
      r->hd_body = arena_strdup(lx->arena, body.s != NULL ? body.s : "");
      sb_free(&body);
    }
}

/* ---- Words -------------------------------------------------------------- */

typedef int (*skip_fn)(const char *, size_t, size_t, size_t *);

/* Runs a scanner at buffer index i, fetching more input while the
 * construct is unfinished. Returns WS_OK/WS_ERROR (never INCOMPLETE:
 * running out of input becomes a lexer error).
 */

static int skip_construct(struct lexer_s *lx, skip_fn fn, size_t i,
                          size_t *end, const char *what)
{
  int r;

  while ((r = fn(lx->buf, lx->len, i, end)) == WS_INCOMPLETE)
    {
      if (!lex_more(lx))
        {
          char msg[80];

          snprintf(msg, sizeof(msg), "unexpected end of file "
                   "while looking for %s", what);
          lex_error(lx, true, msg);
          return WS_ERROR;
        }
    }

  if (r != WS_OK)
    {
      lex_error(lx, false, "syntax error in expansion");
    }

  return r;
}

static int lex_word(struct lexer_s *lx, struct token_s *tok)
{
  struct sbuf_s w;
  bool quoted = false;

  sb_init(&w);

  while (lx->pos < lx->len)
    {
      char c = lx->buf[lx->pos];
      size_t start = lx->pos;
      size_t end;
      int r = WS_OK;

      if (is_meta(c))
        {
          break;
        }

      if (c == '\\')
        {
          if (lx->pos + 1 >= lx->len)
            {
              lex_more(lx);
            }

          if (lx->pos + 1 < lx->len && lx->buf[lx->pos + 1] == '\n')
            {
              lx->pos += 2;      /* line continuation: vanishes */
              continue;
            }

          quoted = true;
          end = lx->pos + 2 <= lx->len ? lx->pos + 2 : lx->len;
        }
      else if (c == '\'')
        {
          quoted = true;
          r = skip_construct(lx, ws_skip_squote, start, &end, "`''");
        }
      else if (c == '"')
        {
          quoted = true;
          r = skip_construct(lx, ws_skip_dquote, start, &end, "`\"'");
        }
      else if (c == '`')
        {
          r = skip_construct(lx, ws_skip_backtick, start, &end, "`` ` ''");
        }
      else if (c == '$')
        {
          if (lx->pos + 1 >= lx->len)
            {
              lex_more(lx);
            }

          r = skip_construct(lx, ws_skip_dollar, start, &end, "`)' or `}'");
        }
      else
        {
          end = start + 1;
        }

      if (r != WS_OK)
        {
          sb_free(&w);
          tok->type = T_ERROR;
          return -1;
        }

      sb_addn(&w, lx->buf + start, end - start);
      lx->pos = end;
    }

  tok->text = arena_strdup(lx->arena, w.s != NULL ? w.s : "");
  tok->quoted = quoted;
  tok->type = T_WORD;

  /* All digits directly followed by < or > is a file descriptor number. */

  if (!quoted && w.len > 0 && lx->pos < lx->len &&
      (lx->buf[lx->pos] == '<' || lx->buf[lx->pos] == '>'))
    {
      size_t i;

      for (i = 0; i < w.len && w.s[i] >= '0' && w.s[i] <= '9'; i++)
        {
        }

      if (i == w.len)
        {
          tok->type = T_IO_NUMBER;
        }
    }

  sb_free(&w);
  return 0;
}

int lex_token(struct lexer_s *lx, struct token_s *tok)
{
  size_t i;

  tok->text = NULL;
  tok->quoted = false;

  for (; ; )
    {
      while (lx->pos < lx->len)
        {
          char c = lx->buf[lx->pos];

          if (c == ' ' || c == '\t')
            {
              lx->pos++;
            }
          else if (c == '\\' && lx->pos + 1 < lx->len &&
                   lx->buf[lx->pos + 1] == '\n')
            {
              lx->pos += 2;
            }
          else
            {
              break;
            }
        }

      if (lx->pos >= lx->len)
        {
          if (!lex_more(lx))
            {
              tok->type = T_EOF;
              return 0;
            }

          continue;
        }

      if (lx->buf[lx->pos] == '#')
        {
          while (lx->pos < lx->len && lx->buf[lx->pos] != '\n')
            {
              lx->pos++;
            }

          continue;
        }

      break;
    }

  if (lx->buf[lx->pos] == '\n')
    {
      lx->pos++;
      if (lx->hd_head != NULL)
        {
          lex_read_heredocs(lx);
        }

      tok->type = T_NEWLINE;
      return 0;
    }

  for (i = 0; i < sizeof(g_ops) / sizeof(g_ops[0]); i++)
    {
      size_t n = strlen(g_ops[i].text);

      if (lx->len - lx->pos >= n &&
          strncmp(lx->buf + lx->pos, g_ops[i].text, n) == 0)
        {
          lx->pos += n;
          tok->type = g_ops[i].type;
          return 0;
        }
    }

  return lex_word(lx, tok);
}
