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

/* The value of a variable's text: it is itself an arithmetic expression. */

static long str_value(struct ar_s *a, const char *v)
{
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

static long var_value(struct ar_s *a, const char *name)
{
  return str_value(a, var_get(name));
}

/* ---- Assignable things: name and name[expr] --------------------------------- */

struct lv_s
{
  char name[128];
  bool elem;
  bool bad;              /* a negative subscript out of range: reads 0, stores nothing */
  long idx;
};

/* The end of the [...] that starts at p (which is on the [), or NULL. */

static const char *skip_subscript(const char *p)
{
  int depth = 0;

  for (; *p != '\0'; p++)
    {
      if (*p == '[')
        {
          depth++;
        }
      else if (*p == ']' && --depth == 0)
        {
          return p + 1;
        }
    }

  return NULL;
}

/* Reads `name` or `name[expr]` at a->p (an identifier start). The subscript
 * is evaluated here, once; a negative one counts from the end of the array.
 */

static bool read_lvalue(struct ar_s *a, struct lv_s *lv)
{
  size_t n = 0;

  while (id_char(*a->p) && n < sizeof(lv->name) - 1)
    {
      lv->name[n++] = *a->p++;
    }

  lv->name[n] = '\0';
  lv->elem = false;
  lv->bad = false;
  lv->idx = 0;
  if (*a->p == '[' && vs_feat(VF_BASH_SYNTAX))
    {
      long idx;

      a->p++;
      idx = parse_comma(a);
      ws(a);
      if (*a->p != ']')
        {
          ar_fail(a, "bad array subscript");
          return false;
        }

      a->p++;
      if (idx < 0 && a->skip == 0)
        {
          struct arr_s *arr = var_array(lv->name, false);
          long top = arr != NULL ? arr_max_index(arr)
                                 : (var_get(lv->name) != NULL ? 0 : -1);

          idx += top + 1;
          if (idx < 0 || top < 0)
            {
              vs_err("%s[%ld]: bad array subscript", lv->name, idx - top - 1);
              lv->bad = true;               /* a warning, not an error: as in bash */
            }
        }

      lv->elem = true;
      lv->idx = idx;
    }

  return true;
}

static long lv_value(struct ar_s *a, const struct lv_s *lv)
{
  if (lv->bad)
    {
      return 0;
    }

  if (!lv->elem)
    {
      return var_value(a, lv->name);
    }

  return str_value(a, var_elem_get(lv->name, lv->idx));
}

static bool lv_store(struct ar_s *a, const struct lv_s *lv, long v)
{
  char buf[32];

  if (a->skip != 0 || a->err || lv->bad)
    {
      return true;
    }

  snprintf(buf, sizeof(buf), "%ld", v);
  if ((lv->elem ? var_elem_set(lv->name, lv->idx, buf) : var_set(lv->name, buf)) != 0)
    {
      a->err = true;
      return false;
    }

  return true;
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
      struct lv_s lv;

      if (!read_lvalue(a, &lv))
        {
          return 0;
        }

      ws(a);
      if (vs_feat(VF_ARITH_EXT) && (strncmp(a->p, "++", 2) == 0 ||
                                    strncmp(a->p, "--", 2) == 0))
        {
          long old = lv_value(a, &lv);

          lv_store(a, &lv, a->p[0] == '+' ? wrap_add(old, 1) : wrap_sub(old, 1));
          a->p += 2;
          return old;
        }

      return lv_value(a, &lv);
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
      struct lv_s lv;
      char op = a->p[0];
      long v;

      a->p += 2;
      if (!read_lvalue(a, &lv))
        {
          return 0;
        }

      v = lv_value(a, &lv);
      v = (op == '+') ? wrap_add(v, 1) : wrap_sub(v, 1);
      lv_store(a, &lv, v);
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
  const char *q;
  char op[4] = "";
  size_t oplen = 0;

  ws(a);
  save = a->p;
  q = a->p;

  /* Look for `name[...] op=` in the text alone: the subscript may have side
   * effects (a[i++]) and must be evaluated once, not once here and once again
   * when it is not an assignment after all.
   */

  if (id_start(*q))
    {
      while (id_char(*q))
        {
          q++;
        }

      if (*q == '[' && vs_feat(VF_BASH_SYNTAX))
        {
          q = skip_subscript(q);
        }

      while (q != NULL && (*q == ' ' || *q == '\t' || *q == '\n'))
        {
          q++;
        }

      if (q == NULL)
        {
          /* an unclosed [ : the normal path reports it */
        }
      else if (q[0] == '=' && q[1] != '=')
        {
          oplen = 1;
        }
      else if (q[0] != '\0' && strchr("+-*/%&^|", q[0]) != NULL && q[1] == '=')   /* strchr matches the NUL! */
        {
          op[0] = q[0];
          oplen = 2;
        }
      else if ((q[0] == '<' || q[0] == '>') && q[1] == q[0] && q[2] == '=')
        {
          op[0] = q[0];
          op[1] = q[0];
          oplen = 3;
        }
    }

  if (oplen == 0)
    {
      a->p = save;
      return parse_ternary(a);
    }

  {
    struct lv_s lv;
    long rhs;
    long result;

    if (!read_lvalue(a, &lv))
      {
        return 0;
      }

    ws(a);
    a->p += oplen;
    rhs = parse_assign(a);
    result = rhs;
    if (op[0] != '\0')
      {
        result = apply(a, op, lv_value(a, &lv), rhs);
      }

    lv_store(a, &lv, result);
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
