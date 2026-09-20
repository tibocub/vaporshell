/*
 * util.c -- allocation wrappers, string buffers, error printing.
 */

#include <nuttx/config.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"

static void oom(void)
{
  fputs("vaporshell: out of memory\n", stderr);
  exit(1);
}

void *vs_xmalloc(size_t n)
{
  void *p = malloc(n != 0 ? n : 1);

  if (p == NULL)
    {
      oom();
    }

  return p;
}

void *vs_xrealloc(void *p, size_t n)
{
  p = realloc(p, n != 0 ? n : 1);
  if (p == NULL)
    {
      oom();
    }

  return p;
}

char *vs_xstrdup(const char *s)
{
  return vs_xstrndup(s, strlen(s));
}

char *vs_xstrndup(const char *s, size_t n)
{
  char *p = vs_xmalloc(n + 1);

  memcpy(p, s, n);
  p[n] = '\0';
  return p;
}

void vs_err(const char *fmt, ...)
{
  va_list ap;

  fflush(stdout);
  fputs("vaporshell: ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}

/* ---- sbuf -------------------------------------------------------------- */

void sb_init(struct sbuf_s *b)
{
  b->s = NULL;
  b->len = 0;
  b->cap = 0;
}

static void sb_reserve(struct sbuf_s *b, size_t extra)
{
  size_t need = b->len + extra + 1;

  if (need > b->cap)
    {
      size_t cap = b->cap != 0 ? b->cap : 32;

      while (cap < need)
        {
          cap *= 2;
        }

      b->s = vs_xrealloc(b->s, cap);
      b->cap = cap;
    }
}

void sb_addn(struct sbuf_s *b, const char *s, size_t n)
{
  sb_reserve(b, n);
  memcpy(b->s + b->len, s, n);
  b->len += n;
  b->s[b->len] = '\0';
}

void sb_addc(struct sbuf_s *b, char c)
{
  sb_addn(b, &c, 1);
}

void sb_adds(struct sbuf_s *b, const char *s)
{
  sb_addn(b, s, strlen(s));
}

char *sb_take(struct sbuf_s *b)
{
  char *s;

  sb_reserve(b, 0);
  b->s[b->len] = '\0';
  s = b->s;
  sb_init(b);
  return s;
}

void sb_free(struct sbuf_s *b)
{
  free(b->s);
  sb_init(b);
}

/* ---- Paths -------------------------------------------------------------- */

/* Lexically resolves "." and ".." (no symlink lookups), making 'path'
 * absolute against 'cwd' if it is relative. Returns 0, or -1 if 'out' is
 * too small. This is what POSIX `cd -L` needs, and what NuttX needs before
 * handing any path with dots to its VFS.
 */

int vs_path_normalize(const char *cwd, const char *path, char *out, size_t n)
{
  size_t len = 0;
  const char *p;

  if (n < 2)
    {
      return -1;
    }

  out[0] = '/';
  out[1] = '\0';
  len = 1;

  p = path;
  if (path[0] != '/' && cwd != NULL)
    {
      /* Start from the working directory: normalize it first. */

      if (vs_path_normalize("/", cwd, out, n) != 0)
        {
          return -1;
        }

      len = strlen(out);
    }

  while (*p != '\0')
    {
      const char *e;
      size_t clen;

      while (*p == '/')
        {
          p++;
        }

      e = p;
      while (*e != '\0' && *e != '/')
        {
          e++;
        }

      clen = (size_t)(e - p);
      if (clen == 0)
        {
          break;
        }

      if (clen == 1 && p[0] == '.')
        {
          /* nothing */
        }
      else if (clen == 2 && p[0] == '.' && p[1] == '.')
        {
          while (len > 1 && out[len - 1] != '/')
            {
              len--;
            }

          if (len > 1)
            {
              len--;                /* the '/' before the removed component */
            }

          out[len] = '\0';
        }
      else
        {
          if (len + 1 + clen + 1 > n)
            {
              return -1;
            }

          if (len > 1)
            {
              out[len++] = '/';
            }

          memcpy(out + len, p, clen);
          len += clen;
          out[len] = '\0';
        }

      p = e;
    }

  if (len == 0)
    {
      out[0] = '/';
      out[1] = '\0';
    }

  return 0;
}
