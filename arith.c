/*
 * arith.c -- $(( )) evaluation: C-style signed long arithmetic, with
 * variables (unset means 0), assignment operators and the ternary
 * operator. Errors are reported and abort the whole expansion.
 */

#include <nuttx/config.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "expand.h"

#define MAX_DEPTH 16

struct ar_s
{
  const char *p;
  bool err;
  int skip;                   /* >0: parsing only (short-circuited branch) */
  int depth;
};

static long parse_assign(struct ar_s *a);

static void ar_fail(struct ar_s *a, const char *msg)
{
  if (!a->err)
    {
      vs_err("arithmetic: %s", msg);
      a->err = true;
    }
}

static void ws(struct ar_s *a)
{
  while (*a->p == ' ' || *a->p == '\t' || *a->p == '\n')
    {
      a->p++;
    }
}

static bool id_start(char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool id_char(char c)
{
  return id_start(c) || (c >= '0' && c <= '9');
}

static long wrap_add(long x, long y)
{
  return (long)((unsigned long)x + (unsigned long)y);
}

static long wrap_sub(long x, long y)
{
  return (long)((unsigned long)x - (unsigned long)y);
}

static long wrap_mul(long x, long y)
{
  return (long)((unsigned long)x * (unsigned long)y);
}

/* The value of a variable: a number, or (as in other shells) an
 * expression of its own.
 */

static long var_value(struct ar_s *a, const char *name)
{
  const char *v = var_get(name);
  long r;

  if (v == NULL || v[0] == '\0')
    {
      return 0;
    }

  if (a->depth >= MAX_DEPTH)
    {
      ar_fail(a, "expression recursion level exceeded");
      return 0;
    }

  {
    struct ar_s sub;

    sub.p = v;
    sub.err = false;
    sub.skip = a->skip;
    sub.depth = a->depth + 1;
    r = parse_assign(&sub);
    ws(&sub);
    if (sub.err || *sub.p != '\0')
      {
        ar_fail(a, "invalid value in variable");
        return 0;
      }
  }

  return r;
}

static long parse_primary(struct ar_s *a)
{
  long v;

  ws(a);
  if (*a->p == '(')
    {
      a->p++;
      v = parse_assign(a);
      ws(a);
      if (*a->p != ')')
        {
          ar_fail(a, "missing `)'");
          return 0;
        }

      a->p++;
      return v;
    }

  if (*a->p >= '0' && *a->p <= '9')
    {
      char *end;
      int base = 10;

      if (a->p[0] == '0' && (a->p[1] == 'x' || a->p[1] == 'X'))
        {
          base = 16;
        }
      else if (a->p[0] == '0' && a->p[1] != '\0')
        {
          base = 8;
        }

      v = strtol(a->p, &end, base);
      if (id_char(*end))
        {
          ar_fail(a, "invalid number");
          return 0;
        }

      a->p = end;
      return v;
    }

  if (id_start(*a->p))
    {
      char name[128];
      size_t n = 0;

      while (id_char(*a->p) && n < sizeof(name) - 1)
        {
          name[n++] = *a->p++;
        }

      name[n] = '\0';
      return var_value(a, name);
    }

  ar_fail(a, *a->p == '\0' ? "operand expected" : "syntax error");
  return 0;
}

static long parse_unary(struct ar_s *a)
{
  ws(a);
  if (*a->p == '+')
    {
      a->p++;
      return parse_unary(a);
    }

  if (*a->p == '-')
    {
      a->p++;
      return wrap_sub(0, parse_unary(a));
    }

  if (*a->p == '!')
    {
      a->p++;
      return !parse_unary(a);
    }

  if (*a->p == '~')
    {
      a->p++;
      return ~parse_unary(a);
    }

  return parse_primary(a);
}

static const struct
{
  const char *op;
  int prec;
} g_bin[] =
{
  { "||", 1 }, { "&&", 2 }, { "==", 6 }, { "!=", 6 }, { "<=", 7 },
  { ">=", 7 }, { "<<", 8 }, { ">>", 8 }, { "|", 3 }, { "^", 4 },
  { "&", 5 }, { "<", 7 }, { ">", 7 }, { "+", 9 }, { "-", 9 },
  { "*", 10 }, { "/", 10 }, { "%", 10 }
};

static long apply(struct ar_s *a, const char *op, long x, long y)
{
  if (op[1] == '\0')
    {
      switch (op[0])
        {
          case '|': return x | y;
          case '^': return x ^ y;
          case '&': return x & y;
          case '<': return x < y;
          case '>': return x > y;
          case '+': return wrap_add(x, y);
          case '-': return wrap_sub(x, y);
          case '*': return wrap_mul(x, y);
          case '/':
          case '%':
            if (y == 0)
              {
                if (a->skip == 0)
                  {
                    ar_fail(a, "division by 0");
                  }

                return 0;
              }

            if (y == -1)
              {
                return op[0] == '/' ? wrap_sub(0, x) : 0;
              }

            return op[0] == '/' ? x / y : x % y;
          default: return 0;
        }
    }

  if (strcmp(op, "==") == 0) return x == y;
  if (strcmp(op, "!=") == 0) return x != y;
  if (strcmp(op, "<=") == 0) return x <= y;
  if (strcmp(op, ">=") == 0) return x >= y;
  if (strcmp(op, "<<") == 0)
    {
      return (y >= 0 && y < (long)(sizeof(long) * CHAR_BIT))
             ? (long)((unsigned long)x << y) : 0;
    }

  if (strcmp(op, ">>") == 0)
    {
      return (y >= 0 && y < (long)(sizeof(long) * CHAR_BIT)) ? x >> y
                                                              : (x < 0 ? -1 : 0);
    }

  return 0;
}

static long parse_binary(struct ar_s *a, int min_prec)
{
  long lhs = parse_unary(a);

  for (; ; )
    {
      size_t i;
      int found = -1;

      ws(a);
      for (i = 0; i < sizeof(g_bin) / sizeof(g_bin[0]); i++)
        {
          if (strncmp(a->p, g_bin[i].op, strlen(g_bin[i].op)) == 0)
            {
              /* "a<b" must not be taken for the start of "<<=" etc.,
               * and "a=b" is an assignment, not handled here.
               */

              found = (int)i;
              break;
            }
        }

      if (found < 0 || g_bin[found].prec < min_prec || a->err)
        {
          return lhs;
        }

      {
        const char *op = g_bin[found].op;
        size_t oplen = strlen(op);
        long rhs;

        if (a->p[oplen] == '=' && oplen == 1 && op[0] != '<' && op[0] != '>')
          {
            return lhs;            /* compound assignment: parse_assign's */
          }

        a->p += oplen;
        if (strcmp(op, "||") == 0)
          {
            if (lhs != 0)
              {
                a->skip++;
              }

            rhs = parse_binary(a, g_bin[found].prec + 1);
            if (lhs != 0)
              {
                a->skip--;
              }

            lhs = (lhs != 0 || rhs != 0);
          }
        else if (strcmp(op, "&&") == 0)
          {
            if (lhs == 0)
              {
                a->skip++;
              }

            rhs = parse_binary(a, g_bin[found].prec + 1);
            if (lhs == 0)
              {
                a->skip--;
              }

            lhs = (lhs != 0 && rhs != 0);
          }
        else
          {
            rhs = parse_binary(a, g_bin[found].prec + 1);
            lhs = apply(a, op, lhs, rhs);
          }
      }
    }
}

static long parse_ternary(struct ar_s *a)
{
  long cond = parse_binary(a, 1);
  long x;
  long y;

  ws(a);
  if (*a->p != '?')
    {
      return cond;
    }

  a->p++;
  if (cond == 0)
    {
      a->skip++;
    }

  x = parse_assign(a);
  if (cond == 0)
    {
      a->skip--;
    }

  ws(a);
  if (*a->p != ':')
    {
      ar_fail(a, "expected `:' in conditional expression");
      return 0;
    }

  a->p++;
  if (cond != 0)
    {
      a->skip++;
    }

  y = parse_assign(a);
  if (cond != 0)
    {
      a->skip--;
    }

  return cond != 0 ? x : y;
}

static long parse_assign(struct ar_s *a)
{
  const char *save;
  char name[128];
  size_t n = 0;
  char op[4] = "";
  size_t oplen = 0;

  ws(a);
  save = a->p;

  if (id_start(*a->p))
    {
      while (id_char(*a->p) && n < sizeof(name) - 1)
        {
          name[n++] = *a->p++;
        }

      name[n] = '\0';
      ws(a);

      if (a->p[0] == '=' && a->p[1] != '=')
        {
          oplen = 1;
        }
      else if (strchr("+-*/%&^|", a->p[0]) != NULL && a->p[1] == '=')
        {
          op[0] = a->p[0];
          oplen = 2;
        }
      else if ((a->p[0] == '<' || a->p[0] == '>') && a->p[1] == a->p[0] &&
               a->p[2] == '=')
        {
          op[0] = a->p[0];
          op[1] = a->p[0];
          oplen = 3;
        }
    }

  if (oplen == 0)
    {
      a->p = save;
      return parse_ternary(a);
    }

  a->p += oplen;

  {
    long rhs = parse_assign(a);
    long result = rhs;
    char buf[32];

    if (op[0] != '\0')
      {
        result = apply(a, op, var_value(a, name), rhs);
      }

    if (a->skip == 0 && !a->err)
      {
        snprintf(buf, sizeof(buf), "%ld", result);
        if (var_set(name, buf) != 0)
          {
            a->err = true;
          }
      }

    return result;
  }
}

int arith_eval(const char *expr, long *result)
{
  struct ar_s a;

  a.p = expr;
  a.err = false;
  a.skip = 0;
  a.depth = 0;

  *result = parse_assign(&a);
  ws(&a);
  if (!a.err && *a.p != '\0')
    {
      ar_fail(&a, "syntax error in expression");
    }

  return a.err ? -1 : 0;
}
