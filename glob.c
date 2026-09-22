/*
 * glob.c -- shell pattern matching (*, ?, [...]) and pathname expansion.
 * Used for globbing, `case` patterns and ${var#pat}-style trimming.
 */

#include <nuttx/config.h>
#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "vaporshell.h"
#include "expand.h"
#include "mode.h"
#include "platform.h"

#define MAX_COMPONENTS 64

static bool quoted_at(const char *pq, size_t i)
{
  return pq != NULL && pq[i] != 0;
}

static bool class_match(const char *name, size_t n, int c)
{
  if (n == 5 && strncmp(name, "alpha", n) == 0)  return isalpha(c) != 0;
  if (n == 5 && strncmp(name, "alnum", n) == 0)  return isalnum(c) != 0;
  if (n == 5 && strncmp(name, "digit", n) == 0)  return isdigit(c) != 0;
  if (n == 5 && strncmp(name, "lower", n) == 0)  return islower(c) != 0;
  if (n == 5 && strncmp(name, "upper", n) == 0)  return isupper(c) != 0;
  if (n == 5 && strncmp(name, "space", n) == 0)  return isspace(c) != 0;
  if (n == 5 && strncmp(name, "blank", n) == 0)  return c == ' ' || c == '\t';
  if (n == 5 && strncmp(name, "punct", n) == 0)  return ispunct(c) != 0;
  if (n == 5 && strncmp(name, "print", n) == 0)  return isprint(c) != 0;
  if (n == 5 && strncmp(name, "graph", n) == 0)  return isgraph(c) != 0;
  if (n == 5 && strncmp(name, "cntrl", n) == 0)  return iscntrl(c) != 0;
  if (n == 6 && strncmp(name, "xdigit", n) == 0) return isxdigit(c) != 0;
  return false;
}

/* The character of the pattern at p[j] (plen bytes in all), and its length. */

static long pat_char(const char *p, size_t plen, size_t j, size_t *len)
{
  return vs_mb_charn(p + j, plen - j, len);
}

/* p[pi] is '['. Returns 1 (c matches; *next is past the ']'), 0 (no
 * match), or -1 (no closing ']': the '[' is an ordinary character). c is a
 * character (a code point in a multibyte locale, else a byte); ranges compare
 * code points, which is bash's default (globasciiranges).
 */

static int match_bracket(const char *p, const char *pq, size_t plen,
                         size_t pi, long c, size_t *next)
{
  size_t j = pi + 1;
  bool negate = false;
  bool matched = false;
  bool first = true;

  if (j < plen && (p[j] == '!' || p[j] == '^') && !quoted_at(pq, j))
    {
      negate = true;
      j++;
    }

  for (; j < plen; first = false)
    {
      long lo;
      size_t l1;

      if (p[j] == ']' && !first && !quoted_at(pq, j))
        {
          *next = j + 1;
          return (matched != negate) ? 1 : 0;
        }

      if (p[j] == '[' && j + 1 < plen && p[j + 1] == ':')
        {
          size_t k = j + 2;

          while (k + 1 < plen && !(p[k] == ':' && p[k + 1] == ']'))
            {
              k++;
            }

          if (k + 1 < plen)
            {
              if (vs_mb() ? vs_mb_isclass(c, p + j + 2, k - (j + 2))
                          : class_match(p + j + 2, k - (j + 2), (int)c))
                {
                  matched = true;
                }

              j = k + 2;
              continue;
            }
        }

      lo = pat_char(p, plen, j, &l1);
      if (j + l1 + 1 < plen && p[j + l1] == '-' && p[j + l1 + 1] != ']')
        {
          size_t l2;
          long hi = pat_char(p, plen, j + l1 + 1, &l2);

          if (c >= lo && c <= hi)
            {
              matched = true;
            }

          j += l1 + 1 + l2;
        }
      else
        {
          if (c == lo)
            {
              matched = true;
            }

          j += l1;
        }
    }

  return -1;
}

static bool chr_eq(char a, char b, bool ci)
{
  return a == b || (ci && tolower((unsigned char)a) == tolower((unsigned char)b));
}

/* Does the literal character at p[pi] equal the one at s? *pl and *sl are their
 * lengths in bytes: one each, except in a multibyte locale, where the other case
 * of `é` is `É`.
 */

static bool lit_eq(const char *p, size_t plen, size_t pi, const char *s, bool ci,
                   size_t *pl, size_t *sl)
{
  long pc;
  long sc;

  if (!vs_mb())
    {
      *pl = 1;
      *sl = 1;
      return chr_eq(p[pi], *s, ci);
    }

  pc = pat_char(p, plen, pi, pl);
  sc = vs_mb_char(s, sl);
  return pc == sc || (ci && vs_mb_swapcase(pc) == sc);
}

/* One bracket expression against one character; with ci the other case of
 * the character is tried too.
 */

static int bracket_ci(const char *p, const char *pq, size_t plen, size_t pi,
                      long ch, size_t *next, bool ci)
{
  int r = match_bracket(p, pq, plen, pi, ch, next);

  if (r != 1 && ci)
    {
      long alt = vs_mb_swapcase(ch);

      if (alt != ch)
        {
          size_t n2;

          if (match_bracket(p, pq, plen, pi, alt, &n2) == 1)
            {
              *next = n2;
              return 1;
            }
        }
    }

  return r;
}

static bool basic_match(const char *p, const char *pq, size_t plen, const char *str,
                        bool ci)
{
  size_t pi = 0;
  const char *sp = str;
  const char *star_s = NULL;
  size_t star_p = 0;
  bool have_star = false;

  while (*sp != '\0')
    {
      if (pi < plen)
        {
          char c = p[pi];
          bool q = quoted_at(pq, pi);

          if (!q && c == '*')
            {
              have_star = true;
              star_p = pi++;
              star_s = sp;
              continue;
            }

          if (!q && c == '?')
            {
              pi++;
              sp += vs_mb_len(sp);          /* one character, not one byte */
              continue;
            }

          if (!q && c == '[')
            {
              size_t next;
              size_t cl;
              long wc = vs_mb_char(sp, &cl);
              int r = bracket_ci(p, pq, plen, pi, wc, &next, ci);

              if (r == 1)
                {
                  pi = next;
                  sp += cl;
                  continue;
                }

              if (r == -1 && *sp == '[')
                {
                  pi++;
                  sp++;
                  continue;
                }
            }
          else
            {
              size_t pl;
              size_t sl;

              if (lit_eq(p, plen, pi, sp, ci, &pl, &sl))
                {
                  pi += pl;
                  sp += sl;
                  continue;
                }
            }
        }

      if (have_star)
        {
          pi = star_p + 1;
          star_s += vs_mb_len(star_s);      /* the star takes one more character */
          sp = star_s;
          continue;
        }

      return false;
    }

  while (pi < plen && p[pi] == '*' && !quoted_at(pq, pi))
    {
      pi++;
    }

  return pi == plen;
}

/* ---- extglob: ?(a|b) *(a|b) +(a|b) @(a|b) !(a|b) ------------------------------
 *
 * These need real backtracking, so they get a small recursive matcher of
 * their own, used only when shopt extglob is on and the pattern has one.
 */

#define XM_MAXALT 64

static bool xm(const char *p, const char *pq, size_t pi, size_t pe, const char *s,
               bool ci);

static bool is_ext_op(const char *p, const char *pq, size_t i, size_t pe)
{
  return i + 1 < pe && p[i + 1] == '(' && !quoted_at(pq, i) &&
         !quoted_at(pq, i + 1) && strchr("?*+@!", p[i]) != NULL;
}

static bool has_ext(const char *p, const char *pq, size_t plen)
{
  size_t i;

  for (i = 0; i < plen; i++)
    {
      if (is_ext_op(p, pq, i, plen))
        {
          return true;
        }
    }

  return false;
}

/* The ')' closing the group whose '(' is at 'open'; (size_t)-1 if none. */

static size_t group_end(const char *p, const char *pq, size_t open, size_t pe)
{
  size_t i;
  int depth = 1;

  for (i = open + 1; i < pe; i++)
    {
      if (quoted_at(pq, i))
        {
          continue;
        }

      if (p[i] == '[')
        {
          size_t j = i + 1;

          if (j < pe && (p[j] == '!' || p[j] == '^'))
            {
              j++;
            }

          if (j < pe && p[j] == ']')
            {
              j++;
            }

          while (j < pe && p[j] != ']')
            {
              j++;
            }

          if (j < pe)
            {
              i = j;
            }
        }
      else if (p[i] == '(')
        {
          depth++;
        }
      else if (p[i] == ')' && --depth == 0)
        {
          return i;
        }
    }

  return (size_t)-1;
}

static int split_alts(const char *p, const char *pq, size_t open, size_t close,
                      size_t alts[][2])
{
  size_t start = open + 1;
  size_t i;
  int depth = 0;
  int n = 0;

  for (i = open + 1; i <= close && n < XM_MAXALT; i++)
    {
      if (i < close && quoted_at(pq, i))
        {
          continue;
        }

      if (i < close && p[i] == '(')
        {
          depth++;
        }
      else if (i < close && p[i] == ')')
        {
          depth--;
        }
      else if (i == close || (p[i] == '|' && depth == 0))
        {
          alts[n][0] = start;
          alts[n][1] = i;
          n++;
          start = i + 1;
        }
    }

  return n;
}

/* Does alternative [a0,a1) match exactly the first 'len' characters of s? */

static bool alt_matches(const char *p, const char *pq, size_t a0, size_t a1,
                        const char *s, size_t len, bool ci)
{
  char *tmp = vs_xstrndup(s, len);
  bool r = xm(p, pq, a0, a1, tmp, ci);

  free(tmp);
  return r;
}

/* The length after k: k plus the character that starts there (1 at the end). */

static size_t step(const char *s, size_t k, size_t len)
{
  return k + (k < len ? vs_mb_len(s + k) : 1);
}

static bool xm_repeat(const char *p, const char *pq, size_t alts[][2], int na,
                      size_t rest, size_t pe, const char *s, bool ci, bool need_one)
{
  size_t len = strlen(s);
  size_t k;
  int a;

  if (!need_one && xm(p, pq, rest, pe, s, ci))
    {
      return true;
    }

  for (a = 0; a < na; a++)
    {
      for (k = step(s, 0, len); k <= len; k = step(s, k, len))
        {
          if (alt_matches(p, pq, alts[a][0], alts[a][1], s, k, ci) &&
              xm_repeat(p, pq, alts, na, rest, pe, s + k, ci, false))
            {
              return true;
            }
        }

      if (need_one && alt_matches(p, pq, alts[a][0], alts[a][1], s, 0, ci) &&
          xm(p, pq, rest, pe, s, ci))
        {
          return true;                   /* an alternative that matches "" */
        }
    }

  return false;
}

static bool xm(const char *p, const char *pq, size_t pi, size_t pe, const char *s,
               bool ci)
{
  char c;
  size_t pl = 1;
  size_t sl = 1;

  if (pi == pe)
    {
      return *s == '\0';
    }

  c = p[pi];
  if (is_ext_op(p, pq, pi, pe))
    {
      size_t close = group_end(p, pq, pi + 1, pe);

      if (close != (size_t)-1)
        {
          size_t alts[XM_MAXALT][2];
          int na = split_alts(p, pq, pi + 1, close, alts);
          size_t rest = close + 1;
          size_t len = strlen(s);
          size_t k;
          int a;

          switch (c)
            {
              case '*':
                return xm_repeat(p, pq, alts, na, rest, pe, s, ci, false);

              case '+':
                return xm_repeat(p, pq, alts, na, rest, pe, s, ci, true);

              case '?':
                if (xm(p, pq, rest, pe, s, ci))
                  {
                    return true;
                  }

                /* fall through - one occurrence, exactly as @() */

              case '@':
                for (a = 0; a < na; a++)
                  {
                    for (k = 0; k <= len; k = step(s, k, len))
                      {
                        if (alt_matches(p, pq, alts[a][0], alts[a][1], s, k, ci) &&
                            xm(p, pq, rest, pe, s + k, ci))
                          {
                            return true;
                          }
                      }
                  }

                return false;

              default:                   /* !(...): anything that is not one of them */
                for (k = 0; k <= len; k = step(s, k, len))
                  {
                    bool any = false;

                    for (a = 0; a < na && !any; a++)
                      {
                        any = alt_matches(p, pq, alts[a][0], alts[a][1], s, k, ci);
                      }

                    if (!any && xm(p, pq, rest, pe, s + k, ci))
                      {
                        return true;
                      }
                  }

                return false;
            }
        }
    }

  if (!quoted_at(pq, pi) && c == '*')
    {
      size_t k;
      size_t len = strlen(s);

      for (k = 0; k <= len; k = step(s, k, len))
        {
          if (xm(p, pq, pi + 1, pe, s + k, ci))
            {
              return true;
            }
        }

      return false;
    }

  if (*s == '\0')
    {
      return false;
    }

  if (!quoted_at(pq, pi) && c == '?')
    {
      return xm(p, pq, pi + 1, pe, s + vs_mb_len(s), ci);
    }

  if (!quoted_at(pq, pi) && c == '[')
    {
      size_t next;
      size_t cl;
      long wc = vs_mb_char(s, &cl);
      int r = bracket_ci(p, pq, pe, pi, wc, &next, ci);

      if (r == 1)
        {
          return xm(p, pq, next, pe, s + cl, ci);
        }

      if (r != -1 || *s != '[')
        {
          return false;
        }
    }
  else if (!lit_eq(p, pe, pi, s, ci, &pl, &sl))
    {
      return false;
    }

  return xm(p, pq, pi + pl, pe, s + sl, ci);
}

bool pat_match_ci(const char *p, const char *pq, size_t plen, const char *str,
                  bool ci)
{
  if (g_sh.so_extglob && has_ext(p, pq, plen))
    {
      return xm(p, pq, 0, plen, str, ci);
    }

  return basic_match(p, pq, plen, str, ci);
}

bool pat_match(const char *p, const char *pq, size_t plen, const char *str)
{
  return pat_match_ci(p, pq, plen, str, false);
}

bool pat_has_glob(const char *p, const char *pq, size_t plen)
{
  size_t i;

  for (i = 0; i < plen; i++)
    {
      if (!quoted_at(pq, i) && (p[i] == '*' || p[i] == '?' || p[i] == '['))
        {
          return true;
        }

      if (g_sh.so_extglob && is_ext_op(p, pq, i, plen))
        {
          return true;                   /* +(a|b) @(a) !(a) */
        }
    }

  return false;
}

/* ---- Pathname expansion --------------------------------------------------- */

struct comp_s
{
  size_t off;
  size_t len;
};

struct gctx_s
{
  const char *p;
  const char *pq;
  struct comp_s comps[MAX_COMPONENTS];
  int n;
  struct fieldv_s *res;
  int added;
  bool ci;                      /* nocaseglob */
  bool dotglob;
  bool globstar;
};

static char *join_path(const char *base, const char *name)
{
  size_t bl = strlen(base);
  size_t nl = strlen(name);
  char *r = vs_xmalloc(bl + nl + 2);

  memcpy(r, base, bl);
  if (bl > 0 && base[bl - 1] != '/')
    {
      r[bl++] = '/';
    }

  memcpy(r + bl, name, nl + 1);
  return r;
}

static char *comp_literal(const struct gctx_s *g, const struct comp_s *c)
{
  return vs_xstrndup(g->p + c->off, c->len);
}

/* Insertion sort: directory listings are small, and it avoids depending
 * on qsort() being available everywhere this has to build.
 */

static void sort_names(char **v, size_t n)
{
  size_t i;

  for (i = 1; i < n; i++)
    {
      char *key = v[i];
      size_t j = i;

      while (j > 0 && vs_plat_collate(v[j - 1], key) > 0)
        {
          v[j] = v[j - 1];
          j--;
        }

      v[j] = key;
    }
}

static void walk(struct gctx_s *g, const char *base, int idx);

/* `**` with shopt globstar: zero or more directories. */

static void walk_globstar(struct gctx_s *g, const char *base, int idx, bool last)
{
  DIR *dir;
  struct dirent *ent;
  char **names = NULL;
  size_t nnames = 0;
  size_t i;

  if (!last)
    {
      walk(g, base, idx + 1);              /* ** matched no directory at all */
    }

  dir = opendir(VS_FS(base[0] != '\0' ? base : "."));
  if (dir == NULL)
    {
      return;
    }

  while ((ent = readdir(dir)) != NULL)
    {
      const char *nm = ent->d_name;

      if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0 ||
          (nm[0] == '.' && !g->dotglob))
        {
          continue;
        }

      names = vs_xrealloc(names, (nnames + 1) * sizeof(char *));
      names[nnames++] = vs_xstrdup(nm);
    }

  closedir(dir);
  sort_names(names, nnames);
  for (i = 0; i < nnames; i++)
    {
      char *np = join_path(base, names[i]);
      struct stat st;

      if (last)
        {
          fv_add(g->res, vs_xstrdup(np));
          g->added++;
        }

      /* symbolic links are not followed, as in bash */

      if (lstat(VS_FS(np), &st) == 0 && S_ISDIR(st.st_mode))
        {
          walk(g, np, idx);
        }

      free(np);
      free(names[i]);
    }

  free(names);
}

static void walk(struct gctx_s *g, const char *base, int idx)
{
  const struct comp_s *c;
  bool last;

  if (idx == g->n)
    {
      fv_add(g->res, vs_xstrdup(base));
      g->added++;
      return;
    }

  c = &g->comps[idx];
  last = (idx == g->n - 1);

  if (c->len == 0)
    {
      char *nb = join_path(base, "");

      /* Leading '/' or a doubled/trailing slash. */

      if (idx == 0)
        {
          free(nb);
          nb = vs_xstrdup("/");
        }
      else if (last)
        {
          struct stat st;

          if (stat(VS_FS(base), &st) != 0 || !S_ISDIR(st.st_mode))
            {
              free(nb);
              return;
            }
        }

      walk(g, nb, idx + 1);
      free(nb);
      return;
    }

  if (g->globstar && c->len == 2 && g->p[c->off] == '*' && g->p[c->off + 1] == '*' &&
      !quoted_at(g->pq != NULL ? g->pq + c->off : NULL, 0))
    {
      walk_globstar(g, base, idx, last);
      return;
    }

  if (!pat_has_glob(g->p + c->off, g->pq != NULL ? g->pq + c->off : NULL,
                    c->len))
    {
      char *lit = comp_literal(g, c);
      char *np = join_path(base, lit);
      struct stat st;

      free(lit);
      if (last)
        {
          if (lstat(VS_FS(np), &st) == 0)
            {
              fv_add(g->res, np);
              g->added++;
              return;
            }
        }
      else
        {
          walk(g, np, idx + 1);
        }

      free(np);
      return;
    }

  {
    DIR *dir = opendir(VS_FS(base[0] != '\0' ? base : "."));
    struct dirent *ent;
    char **names = NULL;
    size_t nnames = 0;
    size_t i;
    const char *pp = g->p + c->off;
    const char *pq = g->pq != NULL ? g->pq + c->off : NULL;

    if (dir == NULL)
      {
        return;
      }

    while ((ent = readdir(dir)) != NULL)
      {
        const char *nm = ent->d_name;

        if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0)
          {
            continue;
          }

        if (nm[0] == '.' && pp[0] != '.' && !g->dotglob)
          {
            continue;
          }

        if (pat_match_ci(pp, pq, c->len, nm, g->ci))
          {
            names = vs_xrealloc(names, (nnames + 1) * sizeof(char *));
            names[nnames++] = vs_xstrdup(nm);
          }
      }

    closedir(dir);
    sort_names(names, nnames);

    for (i = 0; i < nnames; i++)
      {
        char *np = join_path(base, names[i]);

        if (last)
          {
            fv_add(g->res, np);
            g->added++;
          }
        else
          {
            struct stat st;

            if (stat(VS_FS(np), &st) == 0 && S_ISDIR(st.st_mode))
              {
                walk(g, np, idx + 1);
              }

            free(np);
          }

        free(names[i]);
      }

    free(names);
  }
}

int glob_expand(const char *p, const char *pq, size_t plen,
                struct fieldv_s *out)
{
  struct gctx_s g;
  size_t i;
  size_t start = 0;

  g.p = p;
  g.pq = pq;
  g.n = 0;
  g.res = out;
  g.added = 0;
  g.ci = g_sh.so_nocaseglob;
  g.dotglob = g_sh.so_dotglob;
  g.globstar = g_sh.so_globstar;

  for (i = 0; i <= plen; i++)
    {
      if (i == plen || p[i] == '/')
        {
          if (g.n >= MAX_COMPONENTS)
            {
              return 0;
            }

          g.comps[g.n].off = start;
          g.comps[g.n].len = i - start;
          g.n++;
          start = i + 1;
        }
    }

  walk(&g, "", 0);

  if (g.globstar && g.added > 1)
    {
      /* bash sorts the results of a ** pattern as complete paths */

      int has = 0;
      size_t k;

      for (k = 0; k + 1 < plen; k++)
        {
          if (p[k] == '*' && p[k + 1] == '*')
            {
              has = 1;
            }
        }

      if (has)
        {
          sort_names(out->v + (out->n - g.added), (size_t)g.added);
        }
    }

  return g.added;
}
