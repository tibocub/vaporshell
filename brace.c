/*
 * brace.c -- brace expansion: a{b,c}d -> abd acd, {1..3}, {a..e}, {01..10..3}.
 *
 * It works on the raw text of a word before any other expansion, exactly as
 * bash does: quotes, backslashes and the $(...) / ${...} / `...` constructs
 * are skipped over, so a brace inside them is never expanded, and the pieces
 * that are copied keep their quoting for the expansions that follow. A brace
 * that is not a valid brace expression (no comma, no sequence, unmatched) is
 * left alone as literal text.
 */

#include <nuttx/config.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "expand.h"
#include "parse.h"

#define BRACE_MAX 100000            /* results from one word */

/* If s[i] starts a quoted or $-construct, the index just past it; else i. */

static size_t skip_construct(const char *s, size_t len, size_t i)
{
  size_t e;

  switch (s[i])
    {
      case '\\':
        return i + 2 <= len ? i + 2 : len;

      case '\'':
        return ws_skip_squote(s, len, i, &e) == WS_OK ? e : len;

      case '"':
        return ws_skip_dquote(s, len, i, &e) == WS_OK ? e : len;

      case '`':
        return ws_skip_backtick(s, len, i, &e) == WS_OK ? e : len;

      case '$':
        if (i + 1 < len && (s[i + 1] == '{' || s[i + 1] == '('))
          {
            return ws_skip_dollar(s, len, i, &e) == WS_OK ? e : len;
          }

        return i;

      default:
        return i;
    }
}

static int expand_into(const char *text, struct fieldv_s *out);

/* A sequence body "x..y" or "x..y..incr". Returns the number of values it
 * produces and fills them into *vals (malloc'd strings), or -1 if 'body' is
 * not a sequence.
 */

static bool parse_int(const char *s, size_t n, long *v, bool *padded)
{
  size_t i = 0;
  bool neg = false;
  long acc = 0;

  if (n == 0)
    {
      return false;
    }

  if (s[0] == '-' || s[0] == '+')
    {
      neg = s[0] == '-';
      i = 1;
    }

  if (i >= n)
    {
      return false;
    }

  *padded = (n - i > 1 && s[i] == '0');
  for (; i < n; i++)
    {
      if (s[i] < '0' || s[i] > '9')
        {
          return false;
        }

      acc = acc * 10 + (s[i] - '0');
    }

  *v = neg ? -acc : acc;
  return true;
}

static int sequence(const char *body, size_t bn, char ***vals)
{
  const char *d1 = NULL;
  const char *d2 = NULL;
  size_t i;
  long a;
  long b;
  long step = 1;
  bool pa = false;
  bool pb = false;
  bool pc = false;
  int width = 0;
  long count;
  char **v;
  long k;

  for (i = 0; i + 1 < bn; i++)
    {
      if (body[i] == '.' && body[i + 1] == '.')
        {
          if (d1 == NULL)
            {
              d1 = body + i;
              i++;
            }
          else if (d2 == NULL)
            {
              d2 = body + i;
              break;
            }
        }
    }

  if (d1 == NULL)
    {
      return -1;
    }

  if (d2 != NULL)
    {
      long st;

      if (!parse_int(d2 + 2, (size_t)(body + bn - (d2 + 2)), &st, &pc))
        {
          return -1;
        }

      step = st < 0 ? -st : st;
      if (step == 0)
        {
          step = 1;
        }
    }

  {
    size_t an = (size_t)(d1 - body);
    size_t bl = (size_t)((d2 != NULL ? d2 : body + bn) - (d1 + 2));

    if (parse_int(body, an, &a, &pa) && parse_int(d1 + 2, bl, &b, &pb))
      {
        if (pa || pb)
          {
            width = (int)(pa ? an : 0);
            if (pb && (int)bl > width)
              {
                width = (int)bl;
              }
          }
      }
    else if (an == 1 && bl == 1 && d1 + 2 < body + bn &&
             ((body[0] >= 'a' && body[0] <= 'z' && d1[2] >= 'a' && d1[2] <= 'z') ||
              (body[0] >= 'A' && body[0] <= 'Z' && d1[2] >= 'A' && d1[2] <= 'Z')))
      {
        a = body[0];
        b = d1[2];
        width = -1;                       /* letters */
      }
    else
      {
        return -1;
      }
  }

  count = (a <= b ? b - a : a - b) / step + 1;
  if (count > BRACE_MAX)
    {
      vs_err("brace expansion: too many elements");
      return -1;
    }

  v = vs_xmalloc((size_t)count * sizeof(char *));
  for (k = 0; k < count; k++)
    {
      long val = a <= b ? a + k * step : a - k * step;
      char buf[48];

      if (width < 0)
        {
          buf[0] = (char)val;
          buf[1] = '\0';
        }
      else if (width > 0)
        {
          snprintf(buf, sizeof(buf), "%0*ld", width, val);
        }
      else
        {
          snprintf(buf, sizeof(buf), "%ld", val);
        }

      v[k] = vs_xstrdup(buf);
    }

  *vals = v;
  return (int)count;
}

/* Finds the first valid brace expression in text; returns false if none. */

static bool find_brace(const char *s, size_t len, size_t *open, size_t *close,
                       size_t *commas, size_t maxc)
{
  size_t i = 0;

  while (i < len)
    {
      size_t e = skip_construct(s, len, i);

      if (e != i)
        {
          i = e;
          continue;
        }

      if (s[i] == '{')
        {
          size_t j = i + 1;
          int depth = 0;
          size_t nc = 0;

          /* `${` was skipped above, so this is a plain brace */

          while (j < len)
            {
              size_t k = skip_construct(s, len, j);

              if (k != j)
                {
                  j = k;
                  continue;
                }

              if (s[j] == '{')
                {
                  depth++;
                }
              else if (s[j] == '}')
                {
                  if (depth == 0)
                    {
                      break;
                    }

                  depth--;
                }
              else if (s[j] == ',' && depth == 0 && nc < maxc)
                {
                  commas[nc++] = j;
                }

              j++;
            }

          if (j < len)
            {
              char **dummy;
              int sq = (nc == 0) ? sequence(s + i + 1, j - i - 1, &dummy) : -1;

              if (nc > 0 || sq > 0)
                {
                  if (sq > 0)
                    {
                      int q;

                      for (q = 0; q < sq; q++)
                        {
                          free(dummy[q]);
                        }

                      free(dummy);
                    }

                  *open = i;
                  *close = j;
                  commas[maxc] = nc;          /* the count travels in the last slot */
                  return true;
                }
            }
        }

      i++;
    }

  return false;
}

static void emit_all(struct fieldv_s *out, const char *pre, size_t pn,
                     const char *mid, size_t mn, const char *post, size_t qn)
{
  char *t = vs_xmalloc(pn + mn + qn + 1);

  memcpy(t, pre, pn);
  memcpy(t + pn, mid, mn);
  memcpy(t + pn + mn, post, qn);
  t[pn + mn + qn] = '\0';
  expand_into(t, out);
  free(t);
}

static int expand_into(const char *text, struct fieldv_s *out)
{
  size_t len = strlen(text);
  size_t open = 0;
  size_t close = 0;
  size_t *commas = vs_xmalloc((len + 2) * sizeof(size_t));   /* [len] carries the count */
  size_t nc;
  size_t i;

  if (out->n >= BRACE_MAX)
    {
      free(commas);
      return -1;
    }

  if (!find_brace(text, len, &open, &close, commas, len))
    {
      free(commas);
      fv_add(out, vs_xstrdup(text));
      return 0;
    }

  nc = commas[len];
  if (nc > 0)
    {
      size_t start = open + 1;

      for (i = 0; i <= nc; i++)
        {
          size_t end = (i < nc) ? commas[i] : close;

          emit_all(out, text, open, text + start, end - start, text + close + 1,
                   len - close - 1);
          start = end + 1;
        }
    }
  else
    {
      char **vals;
      int n = sequence(text + open + 1, close - open - 1, &vals);
      int k;

      for (k = 0; k < n; k++)
        {
          emit_all(out, text, open, vals[k], strlen(vals[k]), text + close + 1,
                   len - close - 1);
          free(vals[k]);
        }

      free(vals);
    }

  free(commas);
  return 0;
}

/* Expands one raw word into out (which is appended to). */

void vs_brace_expand(const char *word, struct fieldv_s *out)
{
  expand_into(word, out);
}
