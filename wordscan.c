/*
 * wordscan.c -- find where a quoted or $-construct ends inside a raw word.
 *
 * Shared by the lexer (deciding where a word stops) and the expander
 * (interpreting it), so the rules for "what is inside quotes / ${} / $()"
 * exist exactly once. The command-substitution case needs the real parser
 * (a ')' can legitimately appear inside a case pattern), so
 * ws_skip_cmdsub() lives in parser.c.
 */

#include <nuttx/config.h>

#include "parse.h"

int ws_skip_squote(const char *s, size_t len, size_t i, size_t *end)
{
  size_t j = i + 1;

  while (j < len)
    {
      if (s[j] == '\'')
        {
          *end = j + 1;
          return WS_OK;
        }

      j++;
    }

  return WS_INCOMPLETE;
}

int ws_skip_backtick(const char *s, size_t len, size_t i, size_t *end)
{
  size_t j = i + 1;

  while (j < len)
    {
      if (s[j] == '\\')
        {
          j += 2;
          continue;
        }

      if (s[j] == '`')
        {
          *end = j + 1;
          return WS_OK;
        }

      j++;
    }

  return WS_INCOMPLETE;
}

int ws_skip_dquote(const char *s, size_t len, size_t i, size_t *end)
{
  size_t j = i + 1;

  while (j < len)
    {
      char c = s[j];

      if (c == '"')
        {
          *end = j + 1;
          return WS_OK;
        }

      if (c == '\\')
        {
          j += 2;
        }
      else if (c == '$' || c == '`')
        {
          size_t e;
          int r = (c == '$') ? ws_skip_dollar(s, len, j, &e)
                             : ws_skip_backtick(s, len, j, &e);

          if (r != WS_OK)
            {
              return r;
            }

          j = e;
        }
      else
        {
          j++;
        }
    }

  return WS_INCOMPLETE;
}

int ws_skip_braced(const char *s, size_t len, size_t i, size_t *end)
{
  size_t j = i;
  bool dq = false;

  while (j < len)
    {
      char c = s[j];
      size_t e;
      int r;

      if (c == '\\')
        {
          j += 2;
        }
      else if (c == '\'' && !dq)
        {
          r = ws_skip_squote(s, len, j, &e);
          if (r != WS_OK)
            {
              return r;
            }

          j = e;
        }
      else if (c == '"')
        {
          dq = !dq;
          j++;
        }
      else if (c == '$' || c == '`')
        {
          r = (c == '$') ? ws_skip_dollar(s, len, j, &e)
                         : ws_skip_backtick(s, len, j, &e);
          if (r != WS_OK)
            {
              return r;
            }

          j = e;
        }
      else if (c == '}' && !dq)
        {
          *end = j + 1;
          return WS_OK;
        }
      else
        {
          j++;
        }
    }

  return WS_INCOMPLETE;
}

/* i is just past "$((" . Arithmetic ends at the matching "))"; if the
 * parentheses close with a lone ')' instead, this was "$( (cmd) )" after
 * all and the caller should retry as a command substitution.
 */

int ws_skip_arith(const char *s, size_t len, size_t i, size_t *end)
{
  size_t j = i;
  int depth = 0;

  while (j < len)
    {
      char c = s[j];

      if (c == '\\')
        {
          j += 2;
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
              if (j + 1 >= len)
                {
                  return WS_INCOMPLETE;
                }

              if (s[j + 1] == ')')
                {
                  *end = j + 2;
                  return WS_OK;
                }

              return WS_ERROR;
            }

          depth--;
        }

      j++;
    }

  return WS_INCOMPLETE;
}

/* i is at a '$'. A plain "$name" or a lone '$' ends immediately (the name
 * characters are ordinary word characters); only the bracketed forms need
 * skipping.
 */

int ws_skip_dollar(const char *s, size_t len, size_t i, size_t *end)
{
  size_t e;
  int r;

  if (i + 1 >= len)
    {
      *end = i + 1;
      return WS_OK;
    }

  if (s[i + 1] == '{')
    {
      r = ws_skip_braced(s, len, i + 2, &e);
    }
  else if (s[i + 1] == '(')
    {
      if (i + 2 < len && s[i + 2] == '(')
        {
          r = ws_skip_arith(s, len, i + 3, &e);
          if (r == WS_ERROR)
            {
              r = ws_skip_cmdsub(s, len, i + 2, &e);
            }
        }
      else
        {
          r = ws_skip_cmdsub(s, len, i + 2, &e);
        }
    }
  else
    {
      *end = i + 1;
      return WS_OK;
    }

  if (r == WS_OK)
    {
      *end = e;
    }

  return r;
}
