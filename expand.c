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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef VAPORSHELL_POSIX
#  include <pwd.h>
#endif

#include "vaporshell.h"
#include "parse.h"
#include "mode.h"
#include "exec.h"
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

/* ${a[i]} reuses the whole scalar ${...} machinery: the element's value is
 * planted here and get_param() hands it out for exactly that name (matched by
 * pointer, so an expansion nested in an operand, or a plain $a, is unaffected).
 */

static const char *g_ov_name;
static const char *g_ov_val;
static bool g_ov_has_idx;            /* the override is ${a[i]}: assigning goes to element i */
static long g_ov_idx;

static bool get_param(const char *name, size_t n, const char **val,
                      char *buf, size_t bufsz)
{
  *val = NULL;

  if (g_ov_name != NULL && name == g_ov_name)
    {
      *val = g_ov_val;
      return g_ov_val != NULL;
    }

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

/* dash treats a malformed ${...} as a fatal syntax error (status 2). */

static void bad_subst_fatal(void)
{
  if (!g_sh.interactive && vs_feat(VF_EXIT2_ON_ERROR))
    {
      g_sh.unwind = UW_EXIT;
      g_sh.last_status = 2;
    }
}

static void param_error(struct xctx_s *x, const char *name, size_t nlen,
                        const char *msg)
{
  vs_err("%.*s: %s", (int)nlen, name, msg);
  x->error = true;
  if (!g_sh.interactive)
    {
      g_sh.unwind = UW_EXIT;
      g_sh.last_status = vs_feat(VF_EXIT2_ON_ERROR) ? 2 : 1;
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

/* ---- ${...} extensions (bash): substring, replace, case, transform, indirect ---- */

static void x_positional(struct xctx_s *x, bool at, bool dq, int from, int to);

static bool ext_op_at(const char *in, size_t i, size_t n)
{
  switch (in[i])
    {
      case ':':
        return i + 1 < n && strchr("-=?+", in[i + 1]) == NULL;
      case '/':
      case '^':
      case ',':
        return true;
      case '@':
        return i + 2 == n;
      default:
        return false;
    }
}

int expand_subscript(const char *sub, size_t n, const char *name, long *idx)
{
  char *text = operand_str(sub, n, false);
  long v = 0;

  if (text == NULL)
    {
      return -1;
    }

  if (text[0] == '\0')
    {
      vs_err("%s: bad array subscript", name != NULL ? name : "");
      free(text);
      return -1;
    }

  if (arith_eval(text, &v) != 0)
    {
      free(text);
      return -1;
    }

  free(text);
  if (v < 0)
    {
      long top = -1;

      if (name != NULL)
        {
          struct arr_s *a = var_array(name, false);

          if (a != NULL)
            {
              top = arr_max_index(a);
            }
          else if (var_get(name) != NULL)
            {
              top = 0;                  /* a scalar has just element 0 */
            }
        }

      v += top + 1;
      if (v < 0 || top < 0)
        {
          vs_err("%s: bad array subscript", name != NULL ? name : "");
          return -1;
        }
    }

  *idx = v;
  return 0;
}

static void ext_fail(struct xctx_s *x, const char *what)
{
  vs_err("%s", what);
  x->error = true;
  bad_subst_fatal();
}

/* "off" or "off:len" as arithmetic. 0 on success. */

static int ext_offsets(const char *spec, size_t sn, long *off, long *len,
                       bool *has_len)
{
  size_t k;
  size_t colon = sn;
  int depth = 0;
  char *a;
  char *b = NULL;

  for (k = 0; k < sn; k++)
    {
      if (spec[k] == '(')
        {
          depth++;
        }
      else if (spec[k] == ')')
        {
          depth--;
        }
      else if (spec[k] == ':' && depth == 0)
        {
          colon = k;
          break;
        }
    }

  *has_len = colon < sn;
  *off = 0;
  *len = 0;
  a = operand_str(spec, colon, false);
  if (a == NULL)
    {
      return -1;
    }

  if (a[0] != '\0' && arith_eval(a, off) != 0)
    {
      free(a);
      return -1;
    }

  free(a);
  if (*has_len)
    {
      b = operand_str(spec + colon + 1, sn - colon - 1, false);
      if (b == NULL || (b[0] != '\0' && arith_eval(b, len) != 0))
        {
          free(b);
          return -1;
        }

      free(b);
    }

  return 0;
}

static void ext_substring(struct xctx_s *x, const char *name, size_t nlen,
                          const char *spec, size_t sn, bool dq)
{
  long off;
  long len;
  bool has_len;

  if (ext_offsets(spec, sn, &off, &len, &has_len) != 0)
    {
      x->error = true;
      return;
    }

  if (nlen == 1 && (name[0] == '@' || name[0] == '*'))
    {
      int npos = g_sh.npos;
      int from;
      int to;

      from = off >= 0 ? (int)off : npos + 1 + (int)off;
      if (off < 0 && from < 1)
        {
          if (has_len && len < 0)
            {
              ext_fail(x, "substring expression < 0");
            }

          return;
        }

      if (from > npos)
        {
          return;
        }

      if (has_len)
        {
          if (len < 0)
            {
              ext_fail(x, "substring expression < 0");
              return;
            }

          to = (int)((long)from + len - 1);
          if (to > npos)
            {
              to = npos;
            }
        }
      else
        {
          to = npos;
        }

      x_positional(x, name[0] == '@', dq, from, to);
      return;
    }

  {
    char buf[32];
    const char *val;
    const char *str = get_param(name, nlen, &val, buf, sizeof(buf)) ? val : "";
    long slen = (long)strlen(str);
    long end;

    if (off < 0)
      {
        off += slen;
        if (off < 0)
          {
            return;                    /* before the start: empty */
          }
      }

    if (off > slen)
      {
        return;
      }

    if (has_len)
      {
        end = len < 0 ? slen + len : off + len;
        if (len < 0 && end < off)
          {
            ext_fail(x, "substring expression < 0");
            return;
          }
      }
    else
      {
        end = slen;
      }

    if (end > slen)
      {
        end = slen;
      }

    {
      char *piece = vs_xstrndup(str + off, (size_t)(end - off));

      x->present = true;
      x_add_value(x, piece, dq);
      free(piece);
    }
  }
}

/* ${var/pat/rep}, ${var//pat/rep}, ${var/#pat/rep}, ${var/%pat/rep}. In the
 * replacement an unquoted & stands for the matched text (bash 5.2+ default).
 */

static char *ext_replace(const char *val, const struct pat_s *pat,
                         const struct pat_s *rep, bool all, bool at_start,
                         bool at_end)
{
  size_t vl = strlen(val);
  char *piece = vs_xmalloc(vl + 1);
  struct sbuf_s out;
  size_t i = 0;
  bool allow_empty = at_start || at_end;

  sb_init(&out);
  while (i <= vl)
    {
      size_t hit = (size_t)-1;
      size_t j;

      if (!(at_start && i != 0))
        {
          for (j = vl; ; j--)
            {
              if ((j > i || allow_empty) && (!at_end || j == vl))
                {
                  memcpy(piece, val + i, j - i);
                  piece[j - i] = '\0';
                  if (pat_match(pat->s, pat->q, pat->len, piece))
                    {
                      hit = j;
                      break;
                    }
                }

              if (j == i)
                {
                  break;
                }
            }
        }

      if (hit == (size_t)-1)
        {
          if (i < vl)
            {
              sb_addc(&out, val[i]);
            }

          i++;
          continue;
        }

      {
        size_t r;

        for (r = 0; r < rep->len; r++)
          {
            if (g_sh.so_patsub && rep->s[r] == '&' && !(rep->q != NULL && rep->q[r]))
              {
                sb_addn(&out, val + i, hit - i);
              }
            else
              {
                sb_addc(&out, rep->s[r]);
              }
          }
      }

      if (!all)
        {
          sb_adds(&out, val + hit);
          i = vl + 1;
          break;
        }

      if (hit == i)
        {
          if (i < vl)
            {
              sb_addc(&out, val[i]);
            }

          i++;
        }
      else
        {
          i = hit;
        }
    }

  free(piece);
  return out.s != NULL ? sb_take(&out) : vs_xstrdup("");
}

static void ext_replace_op(struct xctx_s *x, const char *name, size_t nlen,
                           const char *spec, size_t sn, bool dq)
{
  char buf[32];
  const char *val;
  const char *str = get_param(name, nlen, &val, buf, sizeof(buf)) ? val : "";
  bool all = false;
  bool at_start = false;
  bool at_end = false;
  size_t k = 0;
  size_t slash;
  int depth = 0;
  char *praw;
  char *rraw = NULL;
  struct pat_s pat;
  struct pat_s rep;
  char *result;

  if (k < sn && spec[k] == '/')
    {
      all = true;
      k++;
    }
  else if (k < sn && spec[k] == '#')
    {
      at_start = true;
      k++;
    }
  else if (k < sn && spec[k] == '%')
    {
      at_end = true;
      k++;
    }

  /* the pattern ends at the first unescaped / outside any ${ } or quotes */

  for (slash = k; slash < sn; slash++)
    {
      size_t e = slash;

      if (spec[slash] == '\\')
        {
          slash++;
          continue;
        }

      if (spec[slash] == '\'' || spec[slash] == '"')
        {
          if (spec[slash] == '\'' ? ws_skip_squote(spec, sn, slash, &e) == WS_OK
                                  : ws_skip_dquote(spec, sn, slash, &e) == WS_OK)
            {
              slash = e - 1;
            }

          continue;
        }

      if (spec[slash] == '$' && slash + 1 < sn && spec[slash + 1] == '{')
        {
          if (ws_skip_braced(spec, sn, slash + 2, &e) == WS_OK)
            {
              slash = e - 1;
            }

          continue;
        }

      if (spec[slash] == '{')
        {
          depth++;
        }
      else if (spec[slash] == '}')
        {
          depth--;
        }
      else if (spec[slash] == '/' && depth <= 0)
        {
          break;
        }
    }

  praw = vs_xstrndup(spec + k, slash - k);
  if (slash < sn)
    {
      rraw = vs_xstrndup(spec + slash + 1, sn - slash - 1);
    }

  pat = expand_pattern(praw);
  rep = expand_pattern(rraw != NULL ? rraw : "");
  free(praw);
  free(rraw);

  if (pat.len == 0 && !at_start && !at_end)
    {
      result = vs_xstrdup(str);           /* an empty pattern replaces nothing */
    }
  else
    {
      result = ext_replace(str, &pat, &rep, all, at_start, at_end);
    }

  pat_free(&pat);
  pat_free(&rep);
  x->present = true;
  x_add_value(x, result, dq);
  free(result);
}

/* ${var^} ${var^^} ${var,} ${var,,}, optionally with a pattern that selects
 * which characters change.
 */

static void ext_case(struct xctx_s *x, const char *name, size_t nlen,
                     const char *spec, size_t sn, bool dq)
{
  char buf[32];
  const char *val;
  const char *str = get_param(name, nlen, &val, buf, sizeof(buf)) ? val : "";
  bool upper = spec[0] == '^';
  bool all = sn > 1 && spec[1] == spec[0];
  size_t k = all ? 2 : 1;
  char *praw = vs_xstrndup(spec + k, sn - k);
  struct pat_s pat = expand_pattern(praw);
  struct sbuf_s out;
  size_t i;

  free(praw);
  sb_init(&out);
  for (i = 0; str[i] != '\0'; i++)
    {
      char c = str[i];

      if (all || i == 0)
        {
          char one[2];

          one[0] = c;
          one[1] = '\0';
          if (pat.len == 0 || pat_match(pat.s, pat.q, pat.len, one))
            {
              c = upper ? (char)toupper((unsigned char)c) : (char)tolower((unsigned char)c);
            }
        }

      sb_addc(&out, c);
    }

  pat_free(&pat);
  x->present = true;
  x_add_value(x, out.s != NULL ? out.s : "", dq);
  sb_free(&out);
}

/* ${var@Q} quoting and the other one-letter transforms. */

static char *quote_for_reuse(const char *v)
{
  struct sbuf_s out;
  const char *p;
  bool ctrl = false;

  for (p = v; *p != '\0'; p++)
    {
      if ((unsigned char)*p < 0x20 || *p == 0x7f)
        {
          ctrl = true;
        }
    }

  sb_init(&out);
  if (ctrl)
    {
      sb_adds(&out, "$'");
      for (p = v; *p != '\0'; p++)
        {
          switch (*p)
            {
              case '\\': sb_adds(&out, "\\\\"); break;
              case '\'': sb_adds(&out, "\\'"); break;
              case '\n': sb_adds(&out, "\\n"); break;
              case '\t': sb_adds(&out, "\\t"); break;
              case '\r': sb_adds(&out, "\\r"); break;
              case '\a': sb_adds(&out, "\\a"); break;
              case '\b': sb_adds(&out, "\\b"); break;
              case '\f': sb_adds(&out, "\\f"); break;
              case '\v': sb_adds(&out, "\\v"); break;
              case 033:  sb_adds(&out, "\\E"); break;
              default:
                if ((unsigned char)*p < 0x20 || *p == 0x7f)
                  {
                    char o[8];

                    snprintf(o, sizeof(o), "\\%03o", (unsigned char)*p);
                    sb_adds(&out, o);
                  }
                else
                  {
                    sb_addc(&out, *p);
                  }
            }
        }

      sb_addc(&out, '\'');
    }
  else
    {
      sb_addc(&out, '\'');
      for (p = v; *p != '\0'; p++)
        {
          if (*p == '\'')
            {
              sb_adds(&out, "'\\''");
            }
          else
            {
              sb_addc(&out, *p);
            }
        }

      sb_addc(&out, '\'');
    }

  return sb_take(&out);
}

/* The value quoted so the shell reads it back the same (used by set, ${x@Q}). */

char *vs_quote_word(const char *v)
{
  return quote_for_reuse(v);
}

static void ext_transform(struct xctx_s *x, const char *name, size_t nlen,
                          char op, bool dq)
{
  char buf[32];
  const char *val;
  bool isset = get_param(name, nlen, &val, buf, sizeof(buf));
  const char *str = isset ? val : "";
  char *r = NULL;
  size_t i;

  switch (op)
    {
      case 'Q':
        if (!isset)
          {
            return;
          }

        r = quote_for_reuse(str);
        break;

      case 'U':
      case 'L':
      case 'u':
        r = vs_xstrdup(str);
        for (i = 0; r[i] != '\0'; i++)
          {
            if (op == 'u' && i > 0)
              {
                break;
              }

            r[i] = (char)(op == 'L' ? tolower((unsigned char)r[i])
                                    : toupper((unsigned char)r[i]));
          }

        break;

      case 'E':
        {
          struct sbuf_s dec;
          bool stop = false;

          sb_init(&dec);
          for (i = 0; str[i] != '\0'; )
            {
              if (str[i] == '\\' && str[i + 1] != '\0')
                {
                  i += 1 + vs_esc_one(str + i + 1,
                                      ESC_OCT_PLAIN | ESC_HEXU | ESC_E | ESC_CTRL,
                                      &dec, &stop);
                }
              else
                {
                  sb_addc(&dec, str[i++]);
                }
            }

          r = dec.s != NULL ? sb_take(&dec) : vs_xstrdup("");
        }

        break;

      case 'A':
        {
          char *q;
          char nm[128];
          struct var_s *v;
          struct sbuf_s a;

          if (!isset || nlen >= sizeof(nm))
            {
              return;
            }

          memcpy(nm, name, nlen);
          nm[nlen] = '\0';
          v = var_lookup(nm);
          q = quote_for_reuse(str);
          sb_init(&a);
          if (v != NULL && (v->flags & (VF_EXPORT | VF_READONLY)) != 0)
            {
              sb_adds(&a, "declare -");
              if ((v->flags & VF_READONLY) != 0) sb_addc(&a, 'r');
              if ((v->flags & VF_EXPORT) != 0) sb_addc(&a, 'x');
              sb_addc(&a, ' ');
            }

          sb_adds(&a, nm);
          sb_addc(&a, '=');
          sb_adds(&a, q);
          free(q);
          r = sb_take(&a);
        }

        break;

      case 'a':
        {
          char nm[128];
          struct var_s *v;
          char fl[4];
          int f = 0;

          if (nlen >= sizeof(nm))
            {
              return;
            }

          memcpy(nm, name, nlen);
          nm[nlen] = '\0';
          v = var_lookup(nm);
          if (v != NULL && (v->flags & VF_READONLY) != 0) fl[f++] = 'r';
          if (v != NULL && (v->flags & VF_EXPORT) != 0) fl[f++] = 'x';
          fl[f] = '\0';
          r = vs_xstrdup(fl);
        }

        break;

      default:
        ext_fail(x, "bad substitution");
        return;
    }

  x->present = true;
  x_add_value(x, r, dq);
  free(r);
}

/* Handles the extension operators; the caller has already found the name and
 * seen that 'rest' starts with one (ext_op_at).
 */

static void x_param_ext(struct xctx_s *x, const char *name, size_t nlen,
                        const char *rest, size_t rn, bool dq)
{
  bool positional = nlen == 1 && (name[0] == '@' || name[0] == '*');

  if (positional && rest[0] != ':')
    {
      ext_fail(x, "bad substitution");
      return;
    }

  switch (rest[0])
    {
      case ':':
        ext_substring(x, name, nlen, rest + 1, rn - 1, dq);
        break;
      case '/':
        ext_replace_op(x, name, nlen, rest + 1, rn - 1, dq);
        break;
      case '^':
      case ',':
        ext_case(x, name, nlen, rest, rn, dq);
        break;
      default:
        ext_transform(x, name, nlen, rest[1], dq);
        break;
    }
}

/* ${!name} (indirect) and ${!prefix*} / ${!prefix@} (names). 'in' is the
 * text after the '!'.
 */

static int cmp_names(const void *a, const void *b)
{
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void x_indirect(struct xctx_s *x, const char *in, size_t n, bool dq)
{
  size_t nl = 0;

  while (nl < n && (nl == 0 ? nm_start(in[nl]) : nm_char(in[nl])))
    {
      nl++;
    }

  if (nl == n)
    {
      char buf[32];
      const char *val;
      const char *tv;
      char tbuf[32];
      char nm[128];

      if (nl >= sizeof(nm))
        {
          ext_fail(x, "bad substitution");
          return;
        }

      memcpy(nm, in, nl);
      nm[nl] = '\0';
      if (!get_param(nm, nl, &val, buf, sizeof(buf)))
        {
          vs_err("%s: invalid indirect expansion", nm);
          x->error = true;
          return;
        }

      if (val[0] == '\0')
        {
          vs_err("%s: invalid variable name", nm);
          x->error = true;
          return;
        }

      if (!valid_param(val, strlen(val)))
        {
          vs_err("%s: invalid variable name", val);
          x->error = true;
          return;
        }

      if (get_param(val, strlen(val), &tv, tbuf, sizeof(tbuf)))
        {
          x->present = true;
          x_add_value(x, tv, dq);
        }

      return;
    }

  if (nl == n - 1 && (in[nl] == '*' || in[nl] == '@'))
    {
      const char **names = NULL;
      int cnt = 0;
      int cap = 0;
      struct var_s *v;
      int k;

      for (v = g_sh.vars; v != NULL; v = v->next)
        {
          if (v->value != NULL && strncmp(v->name, in, nl) == 0)
            {
              if (cnt == cap)
                {
                  cap = cap ? cap * 2 : 16;
                  names = vs_xrealloc(names, (size_t)cap * sizeof(*names));
                }

              names[cnt++] = v->name;
            }
        }

      if (cnt > 1)
        {
          qsort(names, (size_t)cnt, sizeof(*names), cmp_names);
        }

      if (dq && in[nl] == '@')
        {
          x->saw_at = true;
          for (k = 0; k < cnt; k++)
            {
              if (k > 0)
                {
                  x_push(x);
                }

              x->present = true;
              x_adds(x, names[k], true);
            }
        }
      else
        {
          const char *ifs = var_get("IFS");
          char sep = (in[nl] == '@' || ifs == NULL) ? ' ' : ifs[0];

          x->present = x->present || cnt > 0;
          for (k = 0; k < cnt; k++)
            {
              if (k > 0 && sep != '\0')
                {
                  x_addc(x, sep, dq);
                }

              x_adds(x, names[k], dq);
            }
        }

      free(names);
      return;
    }

  ext_fail(x, "bad substitution");
}

static void x_positional(struct xctx_s *x, bool at, bool dq, int from, int to);
static void trim_and_add(struct xctx_s *x, const char *value, char op, bool longest,
                         const char *word, size_t wn, bool dq);

/* ---- Multi-valued parameters: $@ $* ${a[@]} ${a[*]} ------------------------------ */

/* The words of a list, as "$@" / "$*" / $@ / $* give them. */

static void x_emit_list(struct xctx_s *x, bool at, bool dq, char *const *vals, int cnt)
{
  int k;

  if (dq && at)
    {
      x->saw_at = true;
      for (k = 0; k < cnt; k++)
        {
          if (k > 0)
            {
              x_push(x);
            }

          x->present = true;
          x_adds(x, vals[k], true);
        }
    }
  else if (dq || !x->split)
    {
      const char *ifs = var_get("IFS");
      char sep = (at || ifs == NULL) ? ' ' : ifs[0];

      x->present = x->present || cnt > 0;
      for (k = 0; k < cnt; k++)
        {
          if (k > 0 && sep != '\0')
            {
              x_addc(x, sep, dq);
            }

          x_adds(x, vals[k], dq);
        }
    }
  else
    {
      for (k = 0; k < cnt; k++)
        {
          if (k > 0)
            {
              x_push(x);
            }

          x_add_value(x, vals[k], false);
        }
    }
}

/* Between the results of an operator applied to each element in turn. */

static void x_multi_between(struct xctx_s *x, bool at, bool dq)
{
  if ((dq && at) || (!dq && x->split))
    {
      x_push(x);                        /* one word per element */
    }
  else
    {
      const char *ifs = var_get("IFS");
      char sep = (at || ifs == NULL) ? ' ' : ifs[0];

      if (sep != '\0')
        {
          x_addc(x, sep, dq);
        }
    }
}

struct mlist_s
{
  char **v;
  long *idx;
  int n;
};

static void mlist_free(struct mlist_s *m)
{
  int k;

  for (k = 0; k < m->n; k++)
    {
      free(m->v[k]);
    }

  free(m->v);
  free(m->idx);
}

/* A private copy of the elements: an operand such as ${a[@]/x/$(...)} may run
 * code, and the array must not move under us.
 */

static void mlist_collect(struct mlist_s *m, const char *name, size_t nlen, bool positional)
{
  m->v = NULL;
  m->idx = NULL;
  m->n = 0;
  if (positional)
    {
      int k;

      m->v = vs_xmalloc((size_t)(g_sh.npos > 0 ? g_sh.npos : 1) * sizeof(char *));
      m->idx = vs_xmalloc((size_t)(g_sh.npos > 0 ? g_sh.npos : 1) * sizeof(long));
      for (k = 1; k <= g_sh.npos; k++)
        {
          m->v[m->n] = vs_xstrdup(pos_get(k));
          m->idx[m->n++] = k;
        }

      return;
    }

  {
    char *nm = vs_xstrndup(name, nlen);
    struct arr_s *a = var_array(nm, false);

    if (a != NULL)
      {
        size_t k;

        m->v = vs_xmalloc((a->n > 0 ? a->n : 1) * sizeof(char *));
        m->idx = vs_xmalloc((a->n > 0 ? a->n : 1) * sizeof(long));
        for (k = 0; k < a->n; k++)
          {
            m->v[m->n] = vs_xstrdup(a->e[k].val);
            m->idx[m->n++] = a->e[k].idx;
          }
      }
    else if (var_get(nm) != NULL)
      {
        m->v = vs_xmalloc(sizeof(char *));
        m->idx = vs_xmalloc(sizeof(long));
        m->v[0] = vs_xstrdup(var_get(nm));
        m->idx[0] = 0;
        m->n = 1;
      }

    free(nm);
  }
}

static void x_multi_bad(struct xctx_s *x, const char *name, size_t nlen, const char *rest,
                        size_t rn)
{
  vs_err("${%.*s%.*s}: bad substitution", (int)nlen, name, (int)rn, rest);
  x->error = true;
  bad_subst_fatal();
}

/* POSIX mode (dash): the operators of ${@...} and ${*...} work on the
 * positional parameters *joined* into one string, so ${@%.txt} trims only the
 * end of the last one, and ${#@} is that string's length. There are no
 * slices and no per-element operators; those are bash's.
 */

static void x_multi_posix(struct xctx_s *x, const char *name, size_t nlen, bool star,
                          const char *rest, size_t rn, bool length, bool dq)
{
  const char *ifs = var_get("IFS");
  char sep = (!star || ifs == NULL) ? ' ' : ifs[0];
  struct sbuf_s joined;
  int k;

  sb_init(&joined);
  for (k = 1; k <= g_sh.npos; k++)
    {
      if (k > 1 && sep != '\0')
        {
          sb_addc(&joined, sep);
        }

      sb_adds(&joined, pos_get(k));
    }

  {
    const char *j = joined.s != NULL ? joined.s : "";
    bool colon = rn > 0 && rest[0] == ':';
    size_t skip = colon ? 1 : 0;
    char op = rn > skip ? rest[skip] : '\0';

    if (length)
      {
        char buf[24];

        if (rn > 0)
          {
            x_multi_bad(x, name, nlen, rest, rn);
          }
        else
          {
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)strlen(j));
            x_add_value(x, buf, dq);
          }
      }
    else if (rn == 0)
      {
        x_positional(x, !star, dq, 1, g_sh.npos);
      }
    else if (op == '-')
      {
        if (colon && j[0] == '\0')
          {
            x_operand(x, rest + skip + 1, rn - skip - 1, dq);
          }
        else
          {
            x_positional(x, !star, dq, 1, g_sh.npos);   /* $@ always counts as set */
          }
      }
    else if (op == '+')
      {
        if (!colon || j[0] != '\0')
          {
            x_operand(x, rest + skip + 1, rn - skip - 1, dq);
          }
      }
    else if (op == '=' || op == '?')
      {
        /* fine while the value is there; when it would have to be assigned
         * (=) or complained about (?) it cannot be: $@ is not a variable
         */

        if (colon && j[0] == '\0')
          {
            x_multi_bad(x, name, nlen, rest, rn);
          }
        else
          {
            x_positional(x, !star, dq, 1, g_sh.npos);
          }
      }
    else if (!colon && (op == '#' || op == '%'))
      {
        bool longest = rn > 1 && rest[1] == op;
        size_t off = longest ? 2 : 1;

        x->present = true;
        trim_and_add(x, j, op, longest, rest + off, rn - off, dq);
      }
    else
      {
        x_multi_bad(x, name, nlen, rest, rn);
      }
  }

  sb_free(&joined);
}

/* ${@...} ${*...} ${a[@]...} ${a[*]...}: 'rest' is what follows the name and
 * subscript. length is a leading # (the count).
 */

static void x_multi(struct xctx_s *x, const char *name, size_t nlen, bool positional,
                    bool star, const char *rest, size_t rn, bool length, bool dq)
{
  struct mlist_s m;
  bool at = !star;
  int k;

  if (positional && !vs_feat(VF_PARAM_EXT))
    {
      x_multi_posix(x, name, nlen, star, rest, rn, length, dq);
      return;
    }

  if (length)
    {
      char buf[24];

      if (rn > 0)
        {
          x_multi_bad(x, name, nlen, rest, rn);
          return;
        }

      mlist_collect(&m, name, nlen, positional);
      snprintf(buf, sizeof(buf), "%d", m.n);
      mlist_free(&m);
      x_add_value(x, buf, dq);
      return;
    }

  /* a slice of the positional parameters keeps its own, verified code */

  if (positional && rn > 0 && rest[0] == ':' && (rn < 2 || strchr("-=?+", rest[1]) == NULL))
    {
      ext_substring(x, name, nlen, rest + 1, rn - 1, dq);
      return;
    }

  if (rn == 0)
    {
      if (positional)
        {
          x_positional(x, at, dq, 1, g_sh.npos);
          return;
        }

      mlist_collect(&m, name, nlen, false);
      x_emit_list(x, at, dq, m.v, m.n);
      mlist_free(&m);
      return;
    }

  mlist_collect(&m, name, nlen, positional);

  /* ${a[@]:off:len}: by index for an array */

  if (rest[0] == ':' && rn >= 2 && strchr("-=?+", rest[1]) == NULL)
    {
      long off;
      long len;
      bool has_len;
      long top = m.n > 0 ? m.idx[m.n - 1] : -1;
      int taken = 0;

      if (ext_offsets(rest + 1, rn - 1, &off, &len, &has_len) != 0)
        {
          x->error = true;
          mlist_free(&m);
          return;
        }

      if (off < 0)
        {
          off += top + 1;
        }

      if (has_len && len < 0)
        {
          ext_fail(x, "substring expression < 0");
          mlist_free(&m);
          return;
        }

      if (off >= 0)
        {
          char **sel = vs_xmalloc((size_t)(m.n > 0 ? m.n : 1) * sizeof(char *));

          for (k = 0; k < m.n && (!has_len || taken < len); k++)
            {
              if (m.idx[k] >= off)
                {
                  sel[taken++] = m.v[k];
                }
            }

          x_emit_list(x, at, dq, sel, taken);
          free(sel);
        }
      else
        {
          x_emit_list(x, at, dq, m.v, 0);
        }

      mlist_free(&m);
      return;
    }

  /* ${a[@]:-w} ${a[@]-w} ${a[@]:+w} ${a[@]+w}: an empty list counts as unset */

  if ((rest[0] == ':' && rn >= 2 && (rest[1] == '-' || rest[1] == '+')) ||
      rest[0] == '-' || rest[0] == '+')
    {
      size_t skip = rest[0] == ':' ? 2 : 1;
      char op = rest[skip - 1];

      if ((op == '-') == (m.n == 0))
        {
          x_operand(x, rest + skip, rn - skip, dq);
        }
      else if (op == '-')
        {
          x_emit_list(x, at, dq, m.v, m.n);
        }

      mlist_free(&m);
      return;
    }

  /* an operator applied to every element */

  {
    char op = rest[0];
    bool longest = false;
    const char *word = NULL;
    size_t wn = 0;
    size_t skip = 1;

    if (op == '#' || op == '%')
      {
        if (rn > 1 && rest[1] == op)
          {
            longest = true;
            skip = 2;
          }

        word = rest + skip;
        wn = rn - skip;
      }
    else if (!(op == '/' || op == '^' || op == ',' ||
               (op == '@' && rn == 2 && strchr("QULuE", rest[1]) != NULL)))
      {
        mlist_free(&m);
        x_multi_bad(x, name, nlen, rest, rn);
        return;
      }

    if (m.n == 0 && dq && at)
      {
        x->saw_at = true;
      }

    for (k = 0; k < m.n && !x->error; k++)
      {
        if (k > 0)
          {
            x_multi_between(x, at, dq);
          }

        x->present = true;
        if (op == '#' || op == '%')
          {
            trim_and_add(x, m.v[k], op, longest, word, wn, dq);
          }
        else
          {
            g_ov_name = name;               /* the operators fetch the value with get_param */
            g_ov_val = m.v[k];
            if (op == '/')
              {
                ext_replace_op(x, name, nlen, rest + 1, rn - 1, dq);
              }
            else if (op == '^' || op == ',')
              {
                ext_case(x, name, nlen, rest, rn, dq);
              }
            else
              {
                ext_transform(x, name, nlen, rest[1], dq);
              }

            g_ov_name = NULL;
            g_ov_val = NULL;
          }
      }
  }

  mlist_free(&m);
}

static void x_braced_inner(struct xctx_s *x, const char *in, size_t n, bool dq)
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

  if (vs_feat(VF_PARAM_EXT) && n > 1 && in[0] == '!' && nm_start(in[1]))
    {
      x_indirect(x, in + 1, n - 1, dq);
      return;
    }

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
      bad_subst_fatal();
      return;
    }

  if (nlen == 1 && (name[0] == '@' || name[0] == '*'))
    {
      /* $@ and $* with or without an operator: one path with ${a[@]} */

      x_multi(x, name, nlen, true, name[0] == '*', in + i, n - i, length, dq);
      return;
    }

  if (vs_feat(VF_PARAM_EXT) && !length && i < n && ext_op_at(in, i, n))
    {
      x_param_ext(x, name, nlen, in + i, n - i, dq);
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
          bad_subst_fatal();
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
              if ((g_ov_name == name && g_ov_has_idx) ? var_elem_set(nm, g_ov_idx, v) != 0
                                                       : var_set(nm, v) != 0)
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

/* $@ and $* over positional parameters from..to (0 is $0, which only a
 * slice such as ${@:0} reaches). "$@" gives one field per parameter; the
 * other forms join with the first character of IFS or split as usual.
 */

static const char *pos_or_arg0(int k)
{
  return k == 0 ? g_sh.arg0 : pos_get(k);
}

static void x_positional(struct xctx_s *x, bool at, bool dq, int from, int to)
{
  int k;

  if (dq && at)
    {
      x->saw_at = true;
      for (k = from; k <= to; k++)
        {
          if (k > from)
            {
              x_push(x);
            }

          x->present = true;
          x_adds(x, pos_or_arg0(k), true);
        }
    }
  else if (dq || !x->split)
    {
      /* Joined into one string: "$*" (and, when nothing splits, $* and $@)
       * use the first character of IFS, or a space.
       */

      const char *ifs = var_get("IFS");
      char sep = (at || ifs == NULL) ? ' ' : ifs[0];

      x->present = x->present || to >= from;
      for (k = from; k <= to; k++)
        {
          if (k > from && sep != '\0')
            {
              x_addc(x, sep, dq);
            }

          x_adds(x, pos_or_arg0(k), dq);
        }
    }
  else
    {
      for (k = from; k <= to; k++)
        {
          if (k > from)
            {
              x_push(x);
            }

          x_add_value(x, pos_or_arg0(k), false);
        }
    }
}

/* ${!a[@]}: the indexes that are set. */

static void x_indices(struct xctx_s *x, const char *name, size_t nlen, bool star, bool dq)
{
  struct mlist_s m;
  char **strs;
  int k;

  mlist_collect(&m, name, nlen, false);
  strs = vs_xmalloc((size_t)(m.n > 0 ? m.n : 1) * sizeof(char *));
  for (k = 0; k < m.n; k++)
    {
      char buf[24];

      snprintf(buf, sizeof(buf), "%ld", m.idx[k]);
      strs[k] = vs_xstrdup(buf);
    }

  x_emit_list(x, !star, dq, strs, m.n);
  for (k = 0; k < m.n; k++)
    {
      free(strs[k]);
    }

  free(strs);
  mlist_free(&m);
}

/* ${...}: the array forms (a subscript after the name) are picked out here;
 * everything else is the scalar code, unchanged.
 */

static void x_braced(struct xctx_s *x, const char *in, size_t n, bool dq)
{
  size_t p = 0;
  size_t s;
  size_t close;
  bool all;
  bool star;
  const char *sub;
  size_t sublen;

  if (!vs_feat(VF_BASH_SYNTAX) || n < 2)
    {
      x_braced_inner(x, in, n, dq);
      return;
    }

  if (in[0] == '#' || in[0] == '!')
    {
      p = 1;
    }

  if (p >= n || !nm_start(in[p]))
    {
      x_braced_inner(x, in, n, dq);
      return;
    }

  for (s = p; s < n && nm_char(in[s]); s++)
    {
    }

  if (s >= n || in[s] != '[')
    {
      x_braced_inner(x, in, n, dq);
      return;
    }

  close = asg_subscript_end(in, n, s);
  if (close == (size_t)-1)
    {
      x_multi_bad(x, in, n, "", 0);
      return;
    }

  sub = in + s + 1;
  sublen = close - s - 1;
  all = sublen == 1 && (sub[0] == '@' || sub[0] == '*');
  star = all && sub[0] == '*';

  if (all)
    {
      if (in[0] == '!')
        {
          if (close + 1 != n)
            {
              x_multi_bad(x, in, n, "", 0);
              return;
            }

          x_indices(x, in + p, s - p, star, dq);
        }
      else
        {
          x_multi(x, in + p, s - p, false, star, in + close + 1, n - close - 1,
                  in[0] == '#', dq);
        }

      return;
    }

  if (in[0] == '!')
    {
      /* ${!a[i]}: the element's value is the name of the variable to expand */

      char *nm = vs_xstrndup(in + p, s - p);
      long ix;
      const char *target;

      if (close + 1 != n || expand_subscript(sub, sublen, nm, &ix) != 0)
        {
          free(nm);
          x_multi_bad(x, in, n, "", 0);
          return;
        }

      target = var_elem_get(nm, ix);
      free(nm);
      if (target == NULL || target[0] == '\0' || !valid_param(target, strlen(target)))
        {
          vs_err("%s: invalid indirect expansion", target != NULL ? target : "");
          x->error = true;
          return;
        }

      {
        const char *tv;
        char tbuf[32];

        if (get_param(target, strlen(target), &tv, tbuf, sizeof(tbuf)))
          {
            x->present = true;
            x_add_value(x, tv, dq);
          }
      }

      return;
    }

  {
    char *nm = vs_xstrndup(in + p, s - p);
    char *in2;
    char *ev;
    long idx;
    const char *save_name = g_ov_name;
    const char *save_val = g_ov_val;
    bool save_has = g_ov_has_idx;
    long save_idx = g_ov_idx;
    const char *cur;

    if (expand_subscript(sub, sublen, nm, &idx) != 0)
      {
        free(nm);
        return;                             /* an error was printed; it expands to nothing */
      }

    cur = var_elem_get(nm, idx);
    ev = cur != NULL ? vs_xstrdup(cur) : NULL;
    free(nm);

    /* the same text without the [subscript], so the scalar code can take over */

    in2 = vs_xmalloc(n + 1);
    memcpy(in2, in, s);
    memcpy(in2 + s, in + close + 1, n - close - 1);
    in2[n - (close + 1 - s)] = '\0';

    g_ov_name = in2 + p;
    g_ov_val = ev;
    g_ov_has_idx = true;
    g_ov_idx = idx;
    x_braced_inner(x, in2, n - (close + 1 - s), dq);
    g_ov_name = save_name;
    g_ov_val = save_val;
    g_ov_has_idx = save_has;
    g_ov_idx = save_idx;
    free(in2);
    free(ev);
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
      x_positional(x, name[0] == '@', dq, 1, g_sh.npos);
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
          g_sh.last_status = vs_feat(VF_EXIT2_ON_ERROR) ? 2 : 1;
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

  if (!dq && s[*i + 1] == '\'' && vs_feat(VF_ANSI_C_QUOTE))
    {
      /* $'...': the escapes are decoded, and the result counts as quoted */

      size_t j = *i + 2;
      struct sbuf_s dec;
      bool stop = false;

      sb_init(&dec);
      while (j < len && s[j] != '\'')
        {
          if (s[j] == '\\' && j + 1 < len)
            {
              j++;
              j += vs_esc_one(s + j, ESC_OCT_PLAIN | ESC_HEXU | ESC_E | ESC_CTRL,
                              &dec, &stop);
            }
          else
            {
              sb_addc(&dec, s[j++]);
            }
        }

      x->present = true;
      if (dec.len > 0)
        {
          x_adds(x, dec.s, true);
        }

      sb_free(&dec);
      *i = j < len ? j + 1 : len;
      return;
    }

  if (!dq && s[*i + 1] == '"' && vs_feat(VF_ANSI_C_QUOTE))
    {
      (*i)++;                       /* $"...": the same as "..." without translation */
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
  bad_subst_fatal();
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

      if (x->glob && !g_sh.opt_f && pat_has_glob(f->s, f->q, f->len))
        {
          if (glob_expand(f->s, f->q, f->len, out) > 0)
            {
              free(f->s);
            }
          else if (g_sh.so_failglob)
            {
              vs_err("no match: %s", f->s);            /* shopt failglob */
              x->error = true;
              free(f->s);
            }
          else if (g_sh.so_nullglob)
            {
              free(f->s);                               /* shopt nullglob: no field */
            }
          else
            {
              fv_add(out, f->s);
            }
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

static int expand_one_word(const char *text, struct fieldv_s *out)
{
  struct xctx_s x;

  x_init(&x);
  x.split = true;
  x.glob = true;
  x_scan(&x, text, strlen(text), false, true);
  if (x.error)
    {
      x_free(&x);
      return -1;
    }

  x_finish(&x, out);
  if (x.error)
    {
      x_free(&x);
      return -1;
    }

  x_free(&x);
  return 0;
}

int expand_words(const struct word_s *w, struct fieldv_s *out)
{
  for (; w != NULL; w = w->next)
    {
      if (vs_feat(VF_BRACE_EXP) && strchr(w->text, '{') != NULL)
        {
          /* brace expansion first, on the raw text; each result is then
           * expanded like any other word
           */

          struct fieldv_s bw;
          int k;

          fv_init(&bw);
          vs_brace_expand(w->text, &bw);
          for (k = 0; k < bw.n; k++)
            {
              if (expand_one_word(bw.v[k], out) != 0)
                {
                  fv_free(&bw);
                  return -1;
                }
            }

          fv_free(&bw);
        }
      else if (expand_one_word(w->text, out) != 0)
        {
          return -1;
        }
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
