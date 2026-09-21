/*
 * readsplit.c -- how `read` divides a line into fields.
 *
 * This is POSIX's field splitting with the twist read adds: with N names the
 * first N-1 fields go one to each and the last name takes what is left; with
 * `read -a` there is no limit. dash and bash agree on every case, which is
 * why one function serves both, and why the rules below are spelled out.
 *
 * IFS characters come in two kinds. IFS whitespace (space, tab, newline that
 * are in IFS) is ignored at both ends of the line and around a delimiter; a
 * run of it counts as one. Any other IFS character is a delimiter of its own:
 * two in a row make an empty field between them.
 *
 * Without -r, `read` keeps a backslash before each character it wants taken
 * literally (bi_read leaves the pair `\c` in the line); such a character never
 * separates fields.
 */

#include <nuttx/config.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "expand.h"

struct rs_s
{
  const char *ifs;
  bool raw;
};

static bool escaped(const struct rs_s *r, const char *p)
{
  return !r->raw && p[0] == '\\' && p[1] != '\0';
}

/* An unescaped IFS character, and the two kinds of it. */

static bool is_ifs(const struct rs_s *r, const char *p)
{
  return *p != '\0' && !escaped(r, p) && strchr(r->ifs, *p) != NULL;
}

static bool is_ws(const struct rs_s *r, const char *p)
{
  return is_ifs(r, p) && strchr(" \t\n", *p) != NULL;
}

static const char *skip_ws(const struct rs_s *r, const char *p)
{
  while (is_ws(r, p))
    {
      p++;
    }

  return p;
}

/* The end of the field that starts at p: the next unescaped IFS character. */

static const char *field_end(const struct rs_s *r, const char *p)
{
  while (*p != '\0' && !is_ifs(r, p))
    {
      p += escaped(r, p) ? 2 : 1;
    }

  return p;
}

/* p is at the IFS character that ended a field: step over that delimiter and
 * the whitespace around it, so p is at the start of the next field, or at the
 * end of the line.
 */

static const char *skip_delimiter(const struct rs_s *r, const char *p)
{
  if (is_ws(r, p))
    {
      p = skip_ws(r, p);
      if (is_ifs(r, p))
        {
          p++;                          /* a non-whitespace delimiter after blanks */
        }
    }
  else
    {
      p++;
    }

  return skip_ws(r, p);
}

/* A copy of [start, end) with the backslashes taken out (unless -r). */

static char *copy_field(const struct rs_s *r, const char *start, const char *end)
{
  char *out = vs_xmalloc((size_t)(end - start) + 1);
  size_t n = 0;

  while (start < end)
    {
      if (escaped(r, start))
        {
          start++;
        }

      out[n++] = *start++;
    }

  out[n] = '\0';
  return out;
}

/* The last name's share: everything left, but without IFS whitespace at the
 * end, and -- if what is left is one field and its terminating delimiter --
 * without that delimiter (`x:` is x; `x::` stays as it is).
 */

static char *last_share(const struct rs_s *r, const char *p)
{
  const char *fe = field_end(r, p);
  const char *keep_end = p;
  const char *q;

  if (*fe != '\0' && *skip_delimiter(r, fe) == '\0')
    {
      return copy_field(r, p, fe);
    }

  for (q = p; *q != '\0'; q += escaped(r, q) ? 2 : 1)
    {
      if (!is_ws(r, q))
        {
          keep_end = q + (escaped(r, q) ? 2 : 1);
        }
    }

  return copy_field(r, p, keep_end);
}

/* Appends the fields of 'line' to 'out'. nvars > 0: exactly that many (the
 * missing ones empty, the last taking the remainder). nvars == 0: all of
 * them, none for a line that has none. nvars < 0: no splitting at all, the one
 * field is the whole line (what `read` with no names puts in REPLY).
 */

void read_split(const char *line, const char *ifs, bool raw, int nvars, struct fieldv_s *out)
{
  struct rs_s r;
  const char *p;
  int k;

  r.ifs = ifs;
  r.raw = raw;
  if (nvars < 0)
    {
      fv_add(out, copy_field(&r, line, line + strlen(line)));
      return;
    }

  p = skip_ws(&r, line);

  if (nvars == 0)
    {
      while (*p != '\0')
        {
          const char *e = field_end(&r, p);

          fv_add(out, copy_field(&r, p, e));
          p = *e != '\0' ? skip_delimiter(&r, e) : e;
        }

      return;
    }

  for (k = 0; k < nvars; k++)
    {
      if (*p == '\0')
        {
          fv_add(out, vs_xstrdup(""));
        }
      else if (k == nvars - 1)
        {
          fv_add(out, last_share(&r, p));
          p += strlen(p);
        }
      else
        {
          const char *e = field_end(&r, p);

          fv_add(out, copy_field(&r, p, e));
          p = *e != '\0' ? skip_delimiter(&r, e) : e;
        }
    }
}
