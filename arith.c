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
#include "mode.h"
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
static long parse_comma(struct ar_s *a);
static long parse_unary(struct ar_s *a);

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
      v = parse_comma(a);
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
      if (*end == '#' && base == 10 && vs_feat(VF_ARITH_EXT) && v >= 2 && v <= 64)
        {
          /* base#digits: 0-9 a-z A-Z @ _ (bases above 36 tell case apart) */

          long acc = 0;
          const char *q = end + 1;
          bool any = false;

          for (; ; q++)
            {
              int d;

              if (*q >= '0' && *q <= '9') d = *q - '0';
              else if (*q >= 'a' && *q <= 'z') d = *q - 'a' + 10;
              else if (*q >= 'A' && *q <= 'Z') d = (v <= 36) ? *q - 'A' + 10 : *q - 'A' + 36;
              else if (*q == '@') d = 62;
              else if (*q == '_') d = 63;
              else break;

              if (d >= v)
                {
                  ar_fail(a, "value too great for base");
                  return 0;
                }

              acc = wrap_add(wrap_mul(acc, v), d);
              any = true;
            }

          if (!any)
            {
              ar_fail(a, "invalid number");
              return 0;
            }

          a->p = q;
          return acc;
        }

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
      ws(a);
      if (vs_feat(VF_ARITH_EXT) && (strncmp(a->p, "++", 2) == 0 ||
                                    strncmp(a->p, "--", 2) == 0))
        {
          long old = var_value(a, name);
          char buf[32];

          snprintf(buf, sizeof(buf), "%ld", a->p[0] == '+' ? wrap_add(old, 1)
                                                            : wrap_sub(old, 1));
          a->p += 2;
          if (a->skip == 0 && !a->err && var_set(name, buf) != 0)
            {
              a->err = true;
            }

          return old;
        }

      return var_value(a, name);
    }

  ar_fail(a, *a->p == '\0' ? "operand expected" : "syntax error");
  return 0;
}

static long ipow(struct ar_s *a, long base, long exp)
{
  long r = 1;

  if (exp < 0)
    {
      ar_fail(a, "exponent less than 0");
      return 0;
    }

  while (exp-- > 0)
    {
      r = wrap_mul(r, base);
    }

  return r;
}

/* Prefix operators bind to their operand first; ** (right associative)
 * then applies to the result, as in bash: -2**2 is 4.
 */

static long parse_prefix(struct ar_s *a)
{
  ws(a);
  if (vs_feat(VF_ARITH_EXT) && (strncmp(a->p, "++", 2) == 0 ||
                                strncmp(a->p, "--", 2) == 0) && id_start(a->p[2]))
    {
      char name[128];
      size_t n = 0;
      char op = a->p[0];
      long v;
      char buf[32];

      a->p += 2;
      while (id_char(*a->p) && n < sizeof(name) - 1)
        {
          name[n++] = *a->p++;
        }

      name[n] = '\0';
      v = var_value(a, name);
      v = (op == '+') ? wrap_add(v, 1) : wrap_sub(v, 1);
      snprintf(buf, sizeof(buf), "%ld", v);
      if (a->skip == 0 && !a->err && var_set(name, buf) != 0)
        {
          a->err = true;
        }

      return v;
    }

  if (*a->p == '+')
    {
      a->p++;
      return parse_prefix(a);
    }

  if (*a->p == '-')
    {
      a->p++;
      return wrap_sub(0, parse_prefix(a));
    }

  if (*a->p == '!')
    {
      a->p++;
      return !parse_prefix(a);
    }

  if (*a->p == '~')
    {
      a->p++;
      return ~parse_prefix(a);
    }

  return parse_primary(a);
}

static long parse_unary(struct ar_s *a)
{
  long base = parse_prefix(a);

  ws(a);
  if (vs_feat(VF_ARITH_EXT) && a->p[0] == '*' && a->p[1] == '*' && a->p[2] != '=')
    {
      a->p += 2;
      return ipow(a, base, parse_unary(a));
    }

  return base;
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

static long parse_comma(struct ar_s *a)
{
  long v = parse_assign(a);

  ws(a);
  while (vs_feat(VF_ARITH_EXT) && *a->p == ',' && !a->err)
    {
      a->p++;
      v = parse_assign(a);
      ws(a);
    }

  return v;
}

int arith_eval(const char *expr, long *result)
{
  struct ar_s a;

  a.p = expr;
  a.err = false;
  a.skip = 0;
  a.depth = 0;

  *result = parse_comma(&a);
  ws(&a);
  if (!a.err && *a.p != '\0')
    {
      ar_fail(&a, "syntax error in expression");
    }

  return a.err ? -1 : 0;
}
