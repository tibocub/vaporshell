/*
 * expand.c -- word expansion.
 *
 * A raw word is scanned once, left to right, building "fields": each
 * character carries a flag saying whether it was quoted. Quoting decides
 * everything downstream: unquoted expansion results are split at IFS,
 * unquoted glob characters are active in pathname expansion, and quote
 * removal at the end is just "drop the flags".
 *
 * Order (POSIX 2.6): tilde, parameter / command / arithmetic expansion
 * (all inline, in the scan), field splitting (inline, as unquoted values
 * are added), pathname expansion (finalize), quote removal.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef VAPORSHELL_POSIX
#  include <pwd.h>
#endif

#include "vaporshell.h"
#include "parse.h"
#include "expand.h"

struct xfield_s
{
  char *s;
  char *q;
  size_t len;
};

struct xctx_s
{
  struct sbuf_s cs;           /* current field: characters */
  struct sbuf_s cq;           /* current field: 1 = quoted */
  bool present;               /* the current field exists (may be empty) */
  bool saw_at;                /* "$@" seen in the current quoted region */
  struct xfield_s *fields;
  int nfields;
  int cap;
  bool split;                 /* IFS-split unquoted expansion results */
  bool glob;
  bool assign_mode;           /* ~ is also special after ':' */
  bool heredoc;               /* quotes are ordinary characters */
  bool error;
};

/* ---- fieldv ------------------------------------------------------------- */

void fv_init(struct fieldv_s *f)
{
  f->v = NULL;
  f->n = 0;
  f->cap = 0;
}

void fv_add(struct fieldv_s *f, char *owned)
{
  if (f->n + 2 > f->cap)
    {
      f->cap = f->cap != 0 ? f->cap * 2 : 8;
      f->v = vs_xrealloc(f->v, (size_t)f->cap * sizeof(char *));
    }

  f->v[f->n++] = owned;
  f->v[f->n] = NULL;
}

void fv_free(struct fieldv_s *f)
{
  int i;

  for (i = 0; i < f->n; i++)
    {
      free(f->v[i]);
    }

  free(f->v);
  fv_init(f);
}

void pat_free(struct pat_s *p)
{
  free(p->s);
  free(p->q);
  p->s = p->q = NULL;
  p->len = 0;
}

/* ---- Field building ------------------------------------------------------ */

static void x_init(struct xctx_s *x)
{
  memset(x, 0, sizeof(*x));
  sb_init(&x->cs);
  sb_init(&x->cq);
}

static void x_addc(struct xctx_s *x, char c, bool quoted)
{
  sb_addc(&x->cs, c);
  sb_addc(&x->cq, quoted ? 1 : 0);
  x->present = true;
}

static void x_adds(struct xctx_s *x, const char *s, bool quoted)
{
  for (; *s != '\0'; s++)
    {
      x_addc(x, *s, quoted);
    }
}

/* Ends the current field, if there is one. */

static void x_push(struct xctx_s *x)
{
  struct xfield_s *f;

  if (!x->present)
    {
      return;
    }

  if (x->nfields == x->cap)
    {
      x->cap = x->cap != 0 ? x->cap * 2 : 4;
      x->fields = vs_xrealloc(x->fields, (size_t)x->cap * sizeof(*x->fields));
    }

  f = &x->fields[x->nfields++];
  f->len = x->cs.len;
  f->s = sb_take(&x->cs);
  f->q = sb_take(&x->cq);
  x->present = false;
}

static void x_free(struct xctx_s *x)
{
  int i;

  for (i = 0; i < x->nfields; i++)
    {
      free(x->fields[i].s);
      free(x->fields[i].q);
    }

  free(x->fields);
  sb_free(&x->cs);
  sb_free(&x->cq);
}

static const char *ifs_chars(void)
{
  const char *ifs = var_get("IFS");

  return ifs != NULL ? ifs : " \t\n";
}

static bool is_ifs(const char *ifs, char c)
{
  return c != '\0' && strchr(ifs, c) != NULL;
}

static bool is_ifs_ws(const char *ifs, char c)
{
  return (c == ' ' || c == '\t' || c == '\n') && is_ifs(ifs, c);
}

/* Adds an expansion result. Quoted (or not splitting): as-is. Otherwise
 * split at IFS: whitespace delimiters coalesce, a non-whitespace
 * delimiter always separates (and swallows adjacent whitespace).
 */

static void x_add_value(struct xctx_s *x, const char *v, bool quoted)
{
  const char *ifs;
  bool after_ws_push = false;

  if (quoted)
    {
      x_adds(x, v, true);
      return;
    }

  if (!x->split)
    {
      x_adds(x, v, false);
      return;
    }

  ifs = ifs_chars();
  for (; *v != '\0'; v++)
    {
      char c = *v;

      if (!is_ifs(ifs, c))
        {
          x_addc(x, c, false);
          after_ws_push = false;
        }
      else if (is_ifs_ws(ifs, c))
        {
          if (x->present)
            {
              x_push(x);
              after_ws_push = true;
            }
        }
      else if (x->present)
        {
          x_push(x);
          after_ws_push = false;
        }
      else if (after_ws_push)
        {
          after_ws_push = false;
        }
      else
        {
          x->present = true;     /* an empty field between delimiters */
          x_push(x);
        }
    }
}

/* ---- Parameters -------------------------------------------------------------- */

static bool nm_start(char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool nm_char(char c)
{
  return nm_start(c) || (c >= '0' && c <= '9');
}

static bool is_special_char(char c)
{
  return c != '\0' && strchr("?$#-!0@*", c) != NULL;
}

/* Looks a parameter up. Returns whether it is set; *val is valid until
 * the next call.
 */

static bool get_param(const char *name, size_t n, const char **val,
                      char *buf, size_t bufsz)
{
  *val = NULL;

  if (n == 1 && is_special_char(name[0]))
    {
      switch (name[0])
        {
          case '?':
            snprintf(buf, bufsz, "%d", g_sh.last_status);
            break;
          case '$':
            snprintf(buf, bufsz, "%ld", (long)g_sh.pid);
            break;
          case '#':
            snprintf(buf, bufsz, "%d", g_sh.npos);
            break;
          case '!':
            if (g_sh.last_bg == 0)
              {
                return false;
              }

            snprintf(buf, bufsz, "%ld", (long)g_sh.last_bg);
            break;
          case '-':
            snprintf(buf, bufsz, "%s%s%s%s%s%s",
                     g_sh.opt_e ? "e" : "", g_sh.opt_f ? "f" : "",
                     g_sh.opt_u ? "u" : "", g_sh.opt_x ? "x" : "",
                     g_sh.opt_C ? "C" : "", g_sh.interactive ? "i" : "");
            break;
          case '0':
            *val = g_sh.arg0;
            return true;
          default:
            return false;
        }

      *val = buf;
      return true;
    }

  if (name[0] >= '0' && name[0] <= '9')
    {
      int idx = 0;
      size_t i;

      for (i = 0; i < n && name[i] >= '0' && name[i] <= '9'; i++)
        {
          idx = idx * 10 + (name[i] - '0');
        }

      *val = pos_get(idx);
      return *val != NULL;
    }

  {
    char tmp[128];

    if (n >= sizeof(tmp))
      {
        return false;
      }

    memcpy(tmp, name, n);
    tmp[n] = '\0';
    *val = var_get(tmp);
    return *val != NULL;
  }
}

static bool all_digits(const char *s, size_t n)
{
  size_t i;

  for (i = 0; i < n; i++)
    {
      if (s[i] < '0' || s[i] > '9')
        {
          return false;
        }
    }

  return n > 0;
}

static bool valid_param(const char *s, size_t n)
{
  return is_valid_name(s, n) || all_digits(s, n) ||
         (n == 1 && is_special_char(s[0]));
}

static void x_scan(struct xctx_s *x, const char *s, size_t len, bool dq,
                   bool word_start);

/* Expands 'word' (a raw operand) in the current context into the field
 * being built.
 */

static void x_operand(struct xctx_s *x, const char *word, size_t n, bool dq)
{
  char *copy = vs_xstrndup(word, n);

  x_scan(x, copy, n, dq, false);
  free(copy);
}

/* The operand as a plain string (no splitting, quotes removed). */

static char *operand_str(const char *word, size_t n, bool heredoc)
{
  char *copy = vs_xstrndup(word, n);
  char *r;

  if (heredoc)
    {
      r = expand_heredoc(copy);
    }
  else
    {
      r = expand_word_str(copy);
    }

  free(copy);
  return r;
}

static void param_error(struct xctx_s *x, const char *name, size_t nlen,
                        const char *msg)
{
  vs_err("%.*s: %s", (int)nlen, name, msg);
  x->error = true;
  if (!g_sh.interactive)
    {
      g_sh.unwind = UW_EXIT;
      g_sh.last_status = 1;
    }
}

static void trim_and_add(struct xctx_s *x, const char *value, char op,
                         bool longest, const char *word, size_t wn, bool dq)
{
  char *wcopy = vs_xstrndup(word, wn);
  struct pat_s pat = expand_pattern(wcopy);
  size_t vl = strlen(value);
  size_t k;
  size_t cut = 0;
  bool found = false;
  char *tmp;

  free(wcopy);
  tmp = vs_xmalloc(vl + 1);

  if (op == '#')
    {
      for (k = 0; k <= vl && !found; k++)
        {
          size_t len = longest ? vl - k : k;

          memcpy(tmp, value, len);
          tmp[len] = '\0';
          if (pat_match(pat.s, pat.q, pat.len, tmp))
            {
              cut = len;
              found = true;
            }
        }

      x_add_value(x, value + (found ? cut : 0), dq);
    }
  else
    {
      for (k = 0; k <= vl && !found; k++)
        {
          size_t start = longest ? k : vl - k;

          if (pat_match(pat.s, pat.q, pat.len, value + start))
            {
              cut = start;
              found = true;
            }
        }

      memcpy(tmp, value, vl + 1);
      if (found)
        {
          tmp[cut] = '\0';
        }

      x_add_value(x, tmp, dq);
    }

  free(tmp);
  pat_free(&pat);
}

/* ${...}: 'in' is the text between the braces. */

static void x_braced(struct xctx_s *x, const char *in, size_t n, bool dq)
{
  const char *name;
  size_t nlen;
  size_t i = 0;
  bool length = false;
  bool colon = false;
  char op = '\0';
  bool longest = false;
  const char *val;
  char buf[32];
  bool isset;
  const char *word = NULL;
  size_t wn = 0;

  if (n > 1 && in[0] == '#')
    {
      length = true;
      i = 1;
    }

  name = in + i;
  if (i < n && (in[i] >= '0' && in[i] <= '9'))
    {
      while (i < n && in[i] >= '0' && in[i] <= '9')
        {
          i++;
        }
    }
  else if (i < n && is_special_char(in[i]) && !(in[i] == '#' && length))
    {
      i++;
    }
  else
    {
      while (i < n && (i == (size_t)(name - in) ? nm_start(in[i])
                                                 : nm_char(in[i])))
        {
          i++;
        }
    }

  nlen = (size_t)(in + i - name);
  if (nlen == 0 || !valid_param(name, nlen))
    {
      vs_err("${%.*s}: bad substitution", (int)n, in);
      x->error = true;
      return;
    }

  if (i < n)
    {
      if (in[i] == ':')
        {
          colon = true;
          i++;
        }

      if (i >= n || strchr("-=?+#%", in[i]) == NULL ||
          (colon && strchr("#%", in[i]) != NULL) || length)
        {
          vs_err("${%.*s}: bad substitution", (int)n, in);
          x->error = true;
          return;
        }

      op = in[i++];
      if ((op == '#' || op == '%') && i < n && in[i] == op)
        {
          longest = true;
          i++;
        }

      word = in + i;
      wn = n - i;
    }

  if (nlen == 1 && (name[0] == '@' || name[0] == '*') && op == '\0')
    {
      if (length)
        {
          snprintf(buf, sizeof(buf), "%d", g_sh.npos);
          x_add_value(x, buf, dq);
        }

      return;
    }

  isset = get_param(name, nlen, &val, buf, sizeof(buf));

  if (length)
    {
      snprintf(buf, sizeof(buf), "%lu",
               (unsigned long)(isset ? strlen(val) : 0));
      x_add_value(x, buf, dq);
      return;
    }

  if (op == '\0')
    {
      if (!isset && g_sh.opt_u && !is_special_char(name[0]))
        {
          param_error(x, name, nlen, "unbound variable");
          return;
        }

      if (isset)
        {
          x_add_value(x, val, dq);
        }

      return;
    }

  {
    bool unset_or_null = !isset || (colon && val[0] == '\0');

    switch (op)
      {
        case '-':
          if (unset_or_null)
            {
              x_operand(x, word, wn, dq);
            }
          else
            {
              x_add_value(x, val, dq);
            }

          break;

        case '+':
          if (!unset_or_null)
            {
              x_operand(x, word, wn, dq);
            }

          break;

        case '=':
          if (unset_or_null)
            {
              char *v;
              char *nm;

              if (!is_valid_name(name, nlen))
                {
                  param_error(x, name, nlen, "cannot assign in this way");
                  return;
                }

              v = operand_str(word, wn, false);
              if (v == NULL)
                {
                  x->error = true;
                  return;
                }

              nm = vs_xstrndup(name, nlen);
              if (var_set(nm, v) != 0)
                {
                  x->error = true;
                }
              else
                {
                  x_add_value(x, v, dq);
                }

              free(nm);
              free(v);
            }
          else
            {
              x_add_value(x, val, dq);
            }

          break;

        case '?':
          if (unset_or_null)
            {
              char *m = operand_str(word, wn, false);

              if (m == NULL)
                {
                  x->error = true;
                  return;
                }

              param_error(x, name, nlen,
                          m[0] != '\0' ? m
                          : (colon ? "parameter null or not set"
                                   : "parameter not set"));
              free(m);
            }
          else
            {
              x_add_value(x, val, dq);
            }

          break;

        case '#':
        case '%':
          trim_and_add(x, isset ? val : "", op, longest, word, wn, dq);
          break;

        default:
          break;
      }
  }
}

/* $name, $1, $@, $* and the other one-character parameters. *i is at the
 * character after '$'.
 */

static void x_simple_param(struct xctx_s *x, const char *s, size_t len,
                           size_t *i, bool dq)
{
  const char *name = s + *i;
  size_t n;
  const char *val;
  char buf[32];
  int k;

  if (nm_start(name[0]))
    {
      n = 1;
      while (*i + n < len && nm_char(name[n]))
        {
          n++;
        }
    }
  else
    {
      n = 1;                    /* a digit or a special character */
    }

  *i += n;

  if (n == 1 && (name[0] == '@' || name[0] == '*'))
    {
      bool at = (name[0] == '@');

      if (dq && at)
        {
          x->saw_at = true;
          for (k = 1; k <= g_sh.npos; k++)
            {
              if (k > 1)
                {
                  x_push(x);
                }

              x->present = true;
              x_adds(x, pos_get(k), true);
            }
        }
      else if (dq || !x->split)
        {
          /* Joined into one string: "$*" (and, when nothing splits, $*
           * and $@) use the first character of IFS, or a space.
           */

          const char *ifs = var_get("IFS");
          char sep = (at || ifs == NULL) ? ' ' : ifs[0];

          x->present = x->present || g_sh.npos > 0;
          for (k = 1; k <= g_sh.npos; k++)
            {
              if (k > 1 && sep != '\0')
                {
                  x_addc(x, sep, dq);
                }

              x_adds(x, pos_get(k), dq);
            }
        }
      else
        {
          for (k = 1; k <= g_sh.npos; k++)
            {
              if (k > 1)
                {
                  x_push(x);
                }

              x_add_value(x, pos_get(k), false);
            }
        }

      return;
    }

  if (get_param(name, n, &val, buf, sizeof(buf)))
    {
      x_add_value(x, val, dq);
    }
  else if (g_sh.opt_u && !is_special_char(name[0]))
    {
      param_error(x, name, n, "unbound variable");
    }
}

/* ---- Command substitution, arithmetic, tilde ---------------------------------- */

static void x_cmdsub(struct xctx_s *x, const char *text, size_t n, bool dq)
{
  char *out = run_cmdsub(text, n);

  x_add_value(x, out, dq);
  free(out);
}

static void x_arith(struct xctx_s *x, const char *expr, size_t n, bool dq)
{
  char *inner = operand_str(expr, n, false);
  long value;
  char buf[32];

  if (inner == NULL)
    {
      x->error = true;
      return;
    }

  if (arith_eval(inner, &value) != 0)
    {
      x->error = true;
      if (!g_sh.interactive)
        {
          g_sh.unwind = UW_EXIT;
          g_sh.last_status = 1;
        }
    }
  else
    {
      snprintf(buf, sizeof(buf), "%ld", value);
      x_add_value(x, buf, dq);
    }

  free(inner);
}

static void x_backtick(struct xctx_s *x, const char *s, size_t n, bool dq)
{
  struct sbuf_s cmd;
  size_t i;

  sb_init(&cmd);
  for (i = 0; i < n; i++)
    {
      if (s[i] == '\\' && i + 1 < n && strchr("$`\\", s[i + 1]) != NULL)
        {
          i++;
        }

      sb_addc(&cmd, s[i]);
    }

  x_cmdsub(x, cmd.s != NULL ? cmd.s : "", cmd.len, dq);
  sb_free(&cmd);
}

/* Returns how many characters of s (at '~') were consumed, or 0 if this
 * is not a tilde prefix.
 */

static size_t x_tilde(struct xctx_s *x, const char *s, size_t len)
{
  size_t j = 1;
  char *user;
  const char *home = NULL;

  while (j < len && s[j] != '/' && !(x->assign_mode && s[j] == ':'))
    {
      if (s[j] == '\'' || s[j] == '"' || s[j] == '\\' || s[j] == '$' ||
          s[j] == '`')
        {
          return 0;
        }

      j++;
    }

  user = vs_xstrndup(s + 1, j - 1);
  if (user[0] == '\0')
    {
      home = var_get("HOME");
    }
  else if (strcmp(user, "+") == 0)
    {
      home = var_get("PWD");
    }
  else if (strcmp(user, "-") == 0)
    {
      home = var_get("OLDPWD");
    }
#ifdef VAPORSHELL_POSIX
  else
    {
      struct passwd *pw = getpwnam(user);

      if (pw != NULL)
        {
          home = pw->pw_dir;
        }
    }
#endif

  free(user);
  if (home == NULL)
    {
      return 0;
    }

  x_adds(x, home, true);
  x->present = true;
  return j;
}

/* ---- The scanner -------------------------------------------------------------- */

static void x_dollar(struct xctx_s *x, const char *s, size_t len, size_t *i,
                     bool dq)
{
  size_t e;
  int r;

  if (*i + 1 >= len)
    {
      x_addc(x, '$', dq);
      (*i)++;
      return;
    }

  if (s[*i + 1] == '{')
    {
      r = ws_skip_braced(s, len, *i + 2, &e);
      if (r == WS_OK)
        {
          x_braced(x, s + *i + 2, e - 1 - (*i + 2), dq);
          *i = e;
          return;
        }
    }
  else if (s[*i + 1] == '(')
    {
      if (*i + 2 < len && s[*i + 2] == '(' &&
          ws_skip_arith(s, len, *i + 3, &e) == WS_OK)
        {
          x_arith(x, s + *i + 3, e - 2 - (*i + 3), dq);
          *i = e;
          return;
        }

      r = ws_skip_cmdsub(s, len, *i + 2, &e);
      if (r == WS_OK)
        {
          x_cmdsub(x, s + *i + 2, e - 1 - (*i + 2), dq);
          *i = e;
          return;
        }
    }
  else if (nm_start(s[*i + 1]) || is_special_char(s[*i + 1]) ||
           (s[*i + 1] >= '0' && s[*i + 1] <= '9'))
    {
      (*i)++;
      x_simple_param(x, s, len, i, dq);
      return;
    }
  else
    {
      x_addc(x, '$', dq);
      (*i)++;
      return;
    }

  vs_err("bad substitution");
  x->error = true;
  *i = len;
}

static void x_scan(struct xctx_s *x, const char *s, size_t len, bool dq,
                   bool word_start)
{
  size_t i = 0;

  while (i < len && !x->error)
    {
      char c = s[i];
      size_t e;

      if (!dq)
        {
          if (c == '\\')
            {
              if (i + 1 < len && s[i + 1] == '\n')
                {
                  i += 2;
                }
              else if (i + 1 < len)
                {
                  x_addc(x, s[i + 1], true);
                  i += 2;
                }
              else
                {
                  x_addc(x, '\\', true);
                  i++;
                }

              continue;
            }

          if (c == '\'' && !x->heredoc)
            {
              if (ws_skip_squote(s, len, i, &e) != WS_OK)
                {
                  e = len;
                }

              x->present = true;
              {
                size_t k;

                for (k = i + 1; k + 1 < e; k++)
                  {
                    x_addc(x, s[k], true);
                  }
              }

              i = e;
              continue;
            }

          if (c == '"' && !x->heredoc)
            {
              bool saved = x->saw_at;

              if (ws_skip_dquote(s, len, i, &e) != WS_OK)
                {
                  e = len + 1;
                }

              x->saw_at = false;
              x_scan(x, s + i + 1, e - i - 2, true, false);
              if (!x->saw_at)
                {
                  x->present = true;
                }

              x->saw_at = saved;
              i = e;
              continue;
            }

          if (c == '~' && ((i == 0 && word_start) ||
                           (x->assign_mode && i > 0 && s[i - 1] == ':')))
            {
              size_t used = x_tilde(x, s + i, len - i);

              if (used > 0)
                {
                  i += used;
                  continue;
                }
            }
        }
      else
        {
          if (c == '\\')
            {
              if (i + 1 < len && s[i + 1] == '\n')
                {
                  i += 2;
                }
              else if (i + 1 < len &&
                       strchr(x->heredoc ? "$`\\" : "$`\"\\", s[i + 1]) != NULL)
                {
                  x_addc(x, s[i + 1], true);
                  i += 2;
                }
              else
                {
                  x_addc(x, '\\', true);
                  i++;
                }

              continue;
            }

          if (c == '"' && !x->heredoc)
            {
              i++;              /* a quote inside a ${...} operand */
              continue;
            }
        }

      if (c == '$')
        {
          x_dollar(x, s, len, &i, dq);
          continue;
        }

      if (c == '`')
        {
          if (ws_skip_backtick(s, len, i, &e) != WS_OK)
            {
              e = len;
              x_backtick(x, s + i + 1, e - i - 1, dq);
            }
          else
            {
              x_backtick(x, s + i + 1, e - i - 2, dq);
            }

          i = e;
          continue;
        }

      x_addc(x, c, dq);
      i++;
    }
}

/* ---- Finishing --------------------------------------------------------------------- */

static void x_finish(struct xctx_s *x, struct fieldv_s *out)
{
  int i;

  x_push(x);
  for (i = 0; i < x->nfields; i++)
    {
      struct xfield_s *f = &x->fields[i];

      if (x->glob && !g_sh.opt_f && pat_has_glob(f->s, f->q, f->len) &&
          glob_expand(f->s, f->q, f->len, out) > 0)
        {
          free(f->s);
        }
      else
        {
          fv_add(out, f->s);
        }

      free(f->q);
      f->s = f->q = NULL;
    }

  x->nfields = 0;
}

int expand_words(const struct word_s *w, struct fieldv_s *out)
{
  for (; w != NULL; w = w->next)
    {
      struct xctx_s x;

      x_init(&x);
      x.split = true;
      x.glob = true;
      x_scan(&x, w->text, strlen(w->text), false, true);
      if (x.error)
        {
          x_free(&x);
          return -1;
        }

      x_finish(&x, out);
      x_free(&x);
    }

  return 0;
}

static char *expand_str(const char *raw, bool assign, bool heredoc)
{
  struct xctx_s x;
  struct fieldv_s f;
  struct sbuf_s r;
  int i;

  x_init(&x);
  x.assign_mode = assign;
  x.heredoc = heredoc;
  fv_init(&f);
  if (heredoc)
    {
      x_scan(&x, raw, strlen(raw), true, false);
    }
  else
    {
      x_scan(&x, raw, strlen(raw), false, true);
    }

  if (x.error)
    {
      x_free(&x);
      return NULL;
    }

  x_finish(&x, &f);
  x_free(&x);

  sb_init(&r);
  for (i = 0; i < f.n; i++)
    {
      if (i > 0)
        {
          sb_addc(&r, ' ');
        }

      sb_adds(&r, f.v[i]);
    }

  fv_free(&f);
  return sb_take(&r);
}

char *expand_word_str(const char *raw)
{
  return expand_str(raw, false, false);
}

char *expand_assign_str(const char *raw)
{
  return expand_str(raw, true, false);
}

char *expand_heredoc(const char *body)
{
  return expand_str(body, false, true);
}

struct pat_s expand_pattern(const char *raw)
{
  struct xctx_s x;
  struct pat_s p;

  x_init(&x);
  x_scan(&x, raw, strlen(raw), false, true);
  p.len = x.cs.len;
  p.s = sb_take(&x.cs);
  p.q = sb_take(&x.cq);
  x_free(&x);
  return p;
}
