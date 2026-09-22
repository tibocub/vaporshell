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
#include "mode.h"
#include "parse.h"

/* Operators, longest first. 'feature' >= 0 means the operator only exists
 * when that feature bit is on (otherwise its characters lex as the shorter
 * operators, exactly as in a shell without the extension).
 */

static const struct
{
  const char *text;
  enum tok_e type;
  int feature;
} g_ops[] =
{
  { "&>>", T_ANDDGREAT, VF_AMP_REDIR }, { "&>", T_ANDGREAT, VF_AMP_REDIR },
  { ";;&", T_DSEMIAMP, VF_BASH_SYNTAX }, { "<<<", T_TLESS, VF_BASH_SYNTAX },
  { ";&", T_SEMIAMP, VF_BASH_SYNTAX },
  { "<<-", T_DLESSDASH, -1 }, { "&&", T_ANDIF, -1 },   { "||", T_ORIF, -1 },
  { ";;", T_DSEMI, -1 },      { "<<", T_DLESS, -1 },   { ">>", T_DGREAT, -1 },
  { "<&", T_LESSAND, -1 },    { ">&", T_GREATAND, -1 }, { "<>", T_LESSGREAT, -1 },
  { ">|", T_CLOBBER, -1 },    { "<", T_LESS, -1 },     { ">", T_GREAT, -1 },
  { ";", T_SEMI, -1 },        { "&", T_AMP, -1 },      { "|", T_PIPE, -1 },
  { "(", T_LPAREN, -1 },      { ")", T_RPAREN, -1 }
};

/* For the parser: an arithmetic command may need more of the input. */

bool lex_fetch(struct lexer_s *lx);

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

/* Is 'w' (the text so far) `name=` or `name+=`, the start of an array
 * assignment when a ( follows?
 */

static bool compound_target(const char *w, size_t len)
{
  size_t n;

  if (len < 2 || w[len - 1] != '=')
    {
      return false;
    }

  n = len - 1;
  if (n > 0 && w[n - 1] == '+')
    {
      n--;
    }

  return n > 0 && is_valid_name(w, n);
}

/* The ( of `a=(` is at lx->pos. Finds the matching ) the way bash reads an
 * array literal: whitespace and newlines separate words, # starts a comment
 * at a word start, and quotes, $(...), ${...} and backticks nest. It may need
 * more input lines. On success *endp is just past the ).
 */

static bool scan_compound(struct lexer_s *lx, size_t *endp)
{
  size_t i = lx->pos + 1;
  int depth = 1;
  bool word_start = true;

  for (; ; )
    {
      char c;

      while (i >= lx->len)
        {
          if (!lex_more(lx))
            {
              lex_error(lx, true, "unexpected end of file while looking for "
                                  "matching `)'");
              return false;
            }
        }

      c = lx->buf[i];
      if (c == ' ' || c == '\t' || c == '\n')
        {
          word_start = true;
          i++;
          continue;
        }

      if (c == '#' && word_start)
        {
          while (i < lx->len && lx->buf[i] != '\n')
            {
              i++;
              if (i >= lx->len)
                {
                  lex_more(lx);          /* the comment runs to the end of its line */
                }
            }

          continue;
        }

      word_start = false;
      if (c == '\\')
        {
          if (i + 1 >= lx->len)
            {
              lex_more(lx);
            }

          i += 2;
          continue;
        }

      if (c == '\'' || c == '"' || c == '`' || c == '$')
        {
          size_t e;
          int r;

          if (c == '$' && i + 1 >= lx->len)
            {
              lex_more(lx);
            }

          if (c == '\'')
            {
              r = skip_construct(lx, ws_skip_squote, i, &e, "`''");
            }
          else if (c == '"')
            {
              r = skip_construct(lx, ws_skip_dquote, i, &e, "`\"'");
            }
          else if (c == '`')
            {
              r = skip_construct(lx, ws_skip_backtick, i, &e, "`` ` ''");
            }
          else
            {
              r = skip_construct(lx, ws_skip_dollar, i, &e, "`)' or `}'");
            }

          if (r != WS_OK)
            {
              return false;
            }

          i = e;
          continue;
        }

      if (c == '(')
        {
          depth++;
        }
      else if (c == ')' && --depth == 0)
        {
          *endp = i + 1;
          return true;
        }

      i++;
    }
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
          /* array assignment: name=( ... ) is one word, newlines and all */

          if (c == '(' && !quoted && vs_feat(VF_BASH_SYNTAX) &&
              compound_target(w.s != NULL ? w.s : "", w.len))
            {
              size_t end;

              if (!scan_compound(lx, &end))
                {
                  sb_free(&w);
                  tok->type = T_ERROR;
                  return -1;
                }

              sb_addn(&w, lx->buf + lx->pos, end - lx->pos);
              lx->pos = end;
              continue;
            }

          /* extglob: ?( *( +( @( !( belong to the word, as far as the ) */

          if (c == '(' && (g_sh.so_extglob || lx->force_extglob) && w.len > 0 &&
              strchr("?*+@!", w.s[w.len - 1]) != NULL)
            {
              size_t j = lx->pos + 1;
              int depth = 1;

              while (j < lx->len && depth > 0)
                {
                  if (lx->buf[j] == '\\')
                    {
                      j += 2;
                    }
                  else
                    {
                      depth += (lx->buf[j] == '(') - (lx->buf[j] == ')');
                      j++;
                    }
                }

              if (depth == 0)
                {
                  sb_addn(&w, lx->buf + lx->pos, j - lx->pos);
                  lx->pos = j;
                  continue;
                }
            }

          /* process substitution: <(...) and >(...) are a word of their
           * own (or part of one -- "cat <(foo)bar" is one word), with a
           * full command list inside, exactly like $(...). ws_skip_cmdsub
           * already parses that; only the offset to it differs.
           */

          if ((c == '<' || c == '>') && lx->pos + 1 < lx->len &&
              lx->buf[lx->pos + 1] == '(' && vs_feat(VF_BASH_SYNTAX))
            {
              size_t e;
              int r2 = skip_construct(lx, ws_skip_procsub, lx->pos, &e, "`)'");

              if (r2 == WS_OK)
                {
                  sb_addn(&w, lx->buf + lx->pos, e - lx->pos);
                  lx->pos = e;
                  continue;
                }

              if (r2 == WS_ERROR)
                {
                  sb_free(&w);
                  tok->type = T_ERROR;
                  return -1;
                }
            }

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

/* 1-based line of buf offset 'off': newlines dropped earlier plus those
 * before it. Counted incrementally; lc_* is reset whenever buf is edited.
 */

static int lex_line_at(struct lexer_s *lx, size_t off)
{
  if (off < lx->lc_off)
    {
      lx->lc_off = 0;
      lx->lc_line = 0;
    }

  for (; lx->lc_off < off && lx->lc_off < lx->len; lx->lc_off++)
    {
      if (lx->buf[lx->lc_off] == '\n')
        {
          lx->lc_line++;
        }
    }

  return lx->line_base + lx->lc_line + 1;
}

static int lex_token_raw(struct lexer_s *lx, struct token_s *tok)
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

  tok->start = lx->pos;
  tok->line = lex_line_at(lx, lx->pos);

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

      if (g_ops[i].feature >= 0 && !vs_feat((enum vs_feature_e)g_ops[i].feature))
        {
          continue;
        }

      /* <(...) and >(...): a word (lex_word knows them), never the plain
       * operator, even at word-start where nothing has been accumulated yet.
       */

      if ((g_ops[i].type == T_LESS || g_ops[i].type == T_GREAT) &&
          lx->pos + 1 < lx->len && lx->buf[lx->pos + 1] == '(' &&
          vs_feat(VF_BASH_SYNTAX))
        {
          continue;
        }

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

bool lex_fetch(struct lexer_s *lx)
{
  return lex_more(lx);
}

int lex_token(struct lexer_s *lx, struct token_s *tok)
{
  int r;

  tok->start = tok->end = lx->pos;
  tok->line = 0;
  r = lex_token_raw(lx, tok);
  tok->end = lx->pos;
  return r;
}
