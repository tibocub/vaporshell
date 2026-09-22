/*
 * mb.c -- characters, for the parts of the shell that count or cut by them
 * (${#v}, ${v:off:len}, case conversion, ? and [...] in patterns, read -n).
 *
 * Everything is built on vs_plat_mbdecode(). When vs_plat_multibyte() is false
 * (NuttX, the C locale) each function is a plain byte operation, so nothing here
 * costs anything or changes behaviour there.
 */

#include <nuttx/config.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "platform.h"

#define MB_MAX 8

bool vs_mb(void)
{
  return vs_plat_multibyte();
}

/* The code point of the character in the first n bytes at s (n > 0), and how
 * many bytes it takes. A byte that is not a valid character, or a sequence cut
 * off by the end, is one character of its own; its "code point" is above the
 * Unicode range so it matches nothing but itself.
 */

long vs_mb_charn(const char *s, size_t n, size_t *len)
{
  long wc = 0;
  size_t r;

  if (n == 0)
    {
      *len = 0;
      return 0;
    }

  if (!vs_plat_multibyte())
    {
      *len = 1;
      return (unsigned char)s[0];
    }

  r = vs_plat_mbdecode(s, n > MB_MAX ? MB_MAX : n, &wc);
  if (r == 0 || r == (size_t)-1)
    {
      *len = 1;
      return 0x110000L + (unsigned char)s[0];
    }

  *len = r;
  return wc;
}

long vs_mb_char(const char *s, size_t *len)
{
  size_t avail = 0;

  while (avail < MB_MAX && s[avail] != '\0')
    {
      avail++;
    }

  if (avail == 0)
    {
      *len = 1;                        /* at the terminator: step over it */
      return 0;
    }

  return vs_mb_charn(s, avail, len);
}

size_t vs_mb_len(const char *s)
{
  size_t len;

  vs_mb_char(s, &len);
  return len;
}

size_t vs_mb_count_n(const char *s, size_t n)
{
  size_t off = 0;
  size_t count = 0;

  if (!vs_plat_multibyte())
    {
      return n;
    }

  while (off < n)
    {
      size_t len;

      vs_mb_charn(s + off, n - off, &len);
      off += len;
      count++;
    }

  return count;
}

size_t vs_mb_count(const char *s)
{
  return vs_mb_count_n(s, strlen(s));
}

/* The byte offset after 'nchars' characters of the nbytes at s, or nbytes if
 * there are fewer.
 */

size_t vs_mb_skip(const char *s, size_t nbytes, size_t nchars)
{
  size_t off = 0;

  if (!vs_plat_multibyte())
    {
      return nchars < nbytes ? nchars : nbytes;
    }

  while (nchars > 0 && off < nbytes)
    {
      size_t len;

      vs_mb_charn(s + off, nbytes - off, &len);
      off += len;
      nchars--;
    }

  return off;
}

/* The byte offset at which each character starts, and one more for the end:
 * bounds[k] is where character k begins, so a search can step by characters.
 * NULL means every byte is a character (the caller uses k itself), and
 * *nchars is then nbytes. Free the result.
 */

size_t *vs_mb_bounds(const char *s, size_t nbytes, size_t *nchars)
{
  size_t *b;
  size_t off = 0;
  size_t k = 0;

  if (!vs_plat_multibyte())
    {
      *nchars = nbytes;
      return NULL;
    }

  b = vs_xmalloc((nbytes + 1) * sizeof(size_t));
  while (off < nbytes)
    {
      size_t len;

      b[k++] = off;
      vs_mb_charn(s + off, nbytes - off, &len);
      off += len;
    }

  b[k] = nbytes;
  *nchars = k;
  return b;
}

/* The other case of a character, as bytes into 'out' (at least 16). The
 * character at s is 'len' bytes. Returns how many bytes were written. In a
 * single-byte locale it is the C library's toupper/tolower of that byte.
 */

size_t vs_mb_case(const char *s, size_t len, bool upper, char *out)
{
  long wc;
  long conv;
  size_t l;
  size_t n;

  if (!vs_plat_multibyte())
    {
      unsigned char c = (unsigned char)s[0];

      out[0] = (char)((c >= 'a' && c <= 'z' && upper) ? c - 'a' + 'A'
                     : (c >= 'A' && c <= 'Z' && !upper) ? c - 'A' + 'a' : c);
      return 1;
    }

  wc = vs_mb_charn(s, len, &l);
  conv = wc >= 0x110000L ? wc : (upper ? vs_plat_wc_toupper(wc) : vs_plat_wc_tolower(wc));
  n = conv == wc ? 0 : vs_plat_mbencode(conv, out);
  if (n == 0)
    {
      memcpy(out, s, len);              /* unchanged, or it has no encoding */
      return len;
    }

  return n;
}

/* The other case of a code point, for matching without regard to case. */

long vs_mb_swapcase(long wc)
{
  long lower;

  if (!vs_plat_multibyte() || wc >= 0x110000L)
    {
      if (wc >= 'a' && wc <= 'z')
        {
          return wc - 'a' + 'A';
        }

      return (wc >= 'A' && wc <= 'Z') ? wc - 'A' + 'a' : wc;
    }

  lower = vs_plat_wc_tolower(wc);
  return lower != wc ? lower : vs_plat_wc_toupper(wc);
}

/* Is the character in the class `[:name:]` (name is n bytes)? */

bool vs_mb_isclass(long wc, const char *name, size_t n)
{
  char buf[16];

  if (n >= sizeof(buf) || wc >= 0x110000L)
    {
      return false;
    }

  memcpy(buf, name, n);
  buf[n] = '\0';
  return vs_plat_wc_isclass(wc, buf);
}
