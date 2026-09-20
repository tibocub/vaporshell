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

/* p[pi] is '['. Returns 1 (c matches; *next is past the ']'), 0 (no
 * match), or -1 (no closing ']': the '[' is an ordinary character).
 */

static int match_bracket(const char *p, const char *pq, size_t plen,
                         size_t pi, int c, size_t *next)
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
      unsigned char lo;

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
              if (class_match(p + j + 2, k - (j + 2), c))
                {
                  matched = true;
                }

              j = k + 2;
              continue;
            }
        }

      lo = (unsigned char)p[j];
      if (j + 2 < plen && p[j + 1] == '-' && p[j + 2] != ']')
        {
          unsigned char hi = (unsigned char)p[j + 2];

          if ((unsigned char)c >= lo && (unsigned char)c <= hi)
            {
              matched = true;
            }

          j += 3;
        }
      else
        {
          if ((unsigned char)c == lo)
            {
              matched = true;
            }

          j++;
        }
    }

  return -1;
}

bool pat_match(const char *p, const char *pq, size_t plen, const char *str)
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
              sp++;
              continue;
            }

          if (!q && c == '[')
            {
              size_t next;
              int r = match_bracket(p, pq, plen, pi, (unsigned char)*sp,
                                    &next);

              if (r == 1)
                {
                  pi = next;
                  sp++;
                  continue;
                }

              if (r == -1 && *sp == '[')
                {
                  pi++;
                  sp++;
                  continue;
                }
            }
          else if (c == *sp)
            {
              pi++;
              sp++;
              continue;
            }
        }

      if (have_star)
        {
          pi = star_p + 1;
          sp = ++star_s;
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

bool pat_has_glob(const char *p, const char *pq, size_t plen)
{
  size_t i;

  for (i = 0; i < plen; i++)
    {
      if (!quoted_at(pq, i) && (p[i] == '*' || p[i] == '?' || p[i] == '['))
        {
          return true;
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

      while (j > 0 && strcmp(v[j - 1], key) > 0)
        {
          v[j] = v[j - 1];
          j--;
        }

      v[j] = key;
    }
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

        if (nm[0] == '.' && pp[0] != '.')
          {
            continue;
          }

        if (pat_match(pp, pq, c->len, nm))
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
  return g.added;
}
