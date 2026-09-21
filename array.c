/*
 * array.c -- bash indexed arrays.
 *
 * An array is sparse: `a[5]=x` on an empty array leaves indexes 0..4 unset,
 * and `${#a[@]}` counts only the elements that are set. So it is kept as the
 * set elements sorted by index, found by binary search; the sizes involved
 * (a script's own arrays) make the O(n) insert cheaper than anything cleverer.
 *
 * Only indexed arrays exist so far. The element type has room for the string
 * key an associative array (`declare -A`) will need.
 */

#include <nuttx/config.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"

struct arr_s *arr_new(void)
{
  struct arr_s *a = vs_xmalloc(sizeof(*a));

  a->e = NULL;
  a->n = 0;
  a->cap = 0;
  a->assoc = false;
  return a;
}

struct arr_s *arr_new_assoc(void)
{
  struct arr_s *a = arr_new();

  a->assoc = true;
  return a;
}

void arr_clear(struct arr_s *a)
{
  size_t i;

  for (i = 0; i < a->n; i++)
    {
      free(a->e[i].val);
      free(a->e[i].key);
    }

  a->n = 0;
}

void arr_free(struct arr_s *a)
{
  if (a != NULL)
    {
      arr_clear(a);
      free(a->e);
      free(a);
    }
}

struct arr_s *arr_clone(const struct arr_s *a)
{
  struct arr_s *c = arr_new();
  size_t i;

  if (a == NULL)
    {
      return c;
    }

  c->e = vs_xmalloc((a->n > 0 ? a->n : 1) * sizeof(*c->e));
  c->cap = a->n > 0 ? a->n : 1;
  for (i = 0; i < a->n; i++)
    {
      c->e[i].idx = a->e[i].idx;
      c->e[i].key = a->e[i].key != NULL ? vs_xstrdup(a->e[i].key) : NULL;
      c->e[i].val = vs_xstrdup(a->e[i].val);
    }

  c->n = a->n;
  c->assoc = a->assoc;
  return c;
}

/* The position of the first element with index >= idx; *found says whether
 * that element has exactly idx.
 */

size_t arr_find(const struct arr_s *a, long idx, bool *found)
{
  size_t lo = 0;
  size_t hi = a->n;

  while (lo < hi)
    {
      size_t mid = lo + (hi - lo) / 2;

      if (a->e[mid].idx < idx)
        {
          lo = mid + 1;
        }
      else
        {
          hi = mid;
        }
    }

  *found = lo < a->n && a->e[lo].idx == idx;
  return lo;
}

const char *arr_get(const struct arr_s *a, long idx)
{
  bool found;
  size_t pos = arr_find(a, idx, &found);

  return found ? a->e[pos].val : NULL;
}

void arr_set(struct arr_s *a, long idx, const char *val)
{
  bool found;
  size_t pos = arr_find(a, idx, &found);
  char *copy = vs_xstrdup(val);

  if (found)
    {
      free(a->e[pos].val);
      a->e[pos].val = copy;
      return;
    }

  if (a->n == a->cap)
    {
      a->cap = a->cap > 0 ? a->cap * 2 : 8;
      a->e = vs_xrealloc(a->e, a->cap * sizeof(*a->e));
    }

  memmove(a->e + pos + 1, a->e + pos, (a->n - pos) * sizeof(*a->e));
  a->e[pos].idx = idx;
  a->e[pos].key = NULL;
  a->e[pos].val = copy;
  a->n++;
}

void arr_unset(struct arr_s *a, long idx)
{
  bool found;
  size_t pos = arr_find(a, idx, &found);

  if (found)
    {
      free(a->e[pos].val);
      free(a->e[pos].key);
      memmove(a->e + pos, a->e + pos + 1, (a->n - pos - 1) * sizeof(*a->e));
      a->n--;
    }
}

/* The highest index that is set, or -1 for an empty array. */

long arr_max_index(const struct arr_s *a)
{
  return a->n > 0 ? a->e[a->n - 1].idx : -1;
}

/* a+=(x) puts each new element one past the highest index in use. */

void arr_append(struct arr_s *a, const char *val)
{
  arr_set(a, arr_max_index(a) + 1, val);
}

/* ---- Associative arrays: found by key, kept in insertion order ------------------ */

static size_t key_find(const struct arr_s *a, const char *key)
{
  size_t i;

  for (i = 0; i < a->n; i++)
    {
      if (strcmp(a->e[i].key, key) == 0)
        {
          return i;
        }
    }

  return a->n;
}

const char *arr_get_key(const struct arr_s *a, const char *key)
{
  size_t i = key_find(a, key);

  return i < a->n ? a->e[i].val : NULL;
}

void arr_set_key(struct arr_s *a, const char *key, const char *val)
{
  size_t i = key_find(a, key);
  char *copy = vs_xstrdup(val);

  if (i < a->n)
    {
      free(a->e[i].val);
      a->e[i].val = copy;
      return;
    }

  if (a->n == a->cap)
    {
      a->cap = a->cap > 0 ? a->cap * 2 : 8;
      a->e = vs_xrealloc(a->e, a->cap * sizeof(*a->e));
    }

  a->e[a->n].idx = 0;
  a->e[a->n].key = vs_xstrdup(key);
  a->e[a->n].val = copy;
  a->n++;
}

void arr_unset_key(struct arr_s *a, const char *key)
{
  size_t i = key_find(a, key);

  if (i < a->n)
    {
      free(a->e[i].val);
      free(a->e[i].key);
      memmove(a->e + i, a->e + i + 1, (a->n - i - 1) * sizeof(*a->e));
      a->n--;
    }
}
