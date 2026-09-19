/*
 * arena.c -- bump allocator. Each parsed command line gets one; the
 * executor releases it when the line is done. Function definitions
 * retain the arena their body lives in.
 */

#include <nuttx/config.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "ast.h"

#define CHUNK_SIZE 4096

struct chunk_s
{
  struct chunk_s *next;
  size_t used;
  size_t size;
  size_t pad;                 /* keeps the payload 8/16-byte aligned */
};

struct arena_s
{
  struct chunk_s *chunks;
  int refs;
};

struct arena_s *arena_new(void)
{
  struct arena_s *a = vs_xmalloc(sizeof(*a));

  a->chunks = NULL;
  a->refs = 1;
  return a;
}

void *arena_alloc(struct arena_s *a, size_t n)
{
  struct chunk_s *c = a->chunks;
  void *p;

  n = (n + 7) & ~(size_t)7;

  if (c == NULL || c->size - c->used < n)
    {
      size_t size = n > CHUNK_SIZE ? n : CHUNK_SIZE;

      c = vs_xmalloc(sizeof(*c) + size);
      c->size = size;
      c->used = 0;
      c->next = a->chunks;
      a->chunks = c;
    }

  p = (char *)(c + 1) + c->used;
  c->used += n;
  memset(p, 0, n);
  return p;
}

char *arena_strndup(struct arena_s *a, const char *s, size_t n)
{
  char *p = arena_alloc(a, n + 1);

  memcpy(p, s, n);
  p[n] = '\0';
  return p;
}

char *arena_strdup(struct arena_s *a, const char *s)
{
  return arena_strndup(a, s, strlen(s));
}

void arena_retain(struct arena_s *a)
{
  a->refs++;
}

void arena_release(struct arena_s *a)
{
  if (--a->refs == 0)
    {
      struct chunk_s *c = a->chunks;

      while (c != NULL)
        {
          struct chunk_s *next = c->next;

          free(c);
          c = next;
        }

      free(a);
    }
}
