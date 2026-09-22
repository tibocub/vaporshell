/*
 * select.c -- the menu `select` prints (bash's multi-column layout, exactly
 * as execute_cmd.c's print_select_list computes it: as many columns as fit
 * the terminal width, but never a single row -- a layout that would fit
 * everything on one line falls back to one item per line instead), and
 * parsing what the person typed against it.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include "vaporshell.h"
#include "expand.h"
#include "platform.h"

#define TABSIZE 8

static int digitlen(int x)
{
  char buf[24];

  return snprintf(buf, sizeof(buf), "%d", x);
}

/* Terminal width: $COLUMNS if a positive integer, else the OS's idea of it,
 * else 80 -- the same order bash's default_columns() uses.
 */

static int term_columns(void)
{
  const char *v = var_get("COLUMNS");
  int c;

  if (v != NULL && v[0] != '\0')
    {
      c = atoi(v);
      if (c > 0)
        {
          return c;
        }
    }

  c = vs_plat_columns();
  return c > 0 ? c : 80;
}

/* Advances the cursor from column 'from' to 'to' with tabs where a tab stop
 * is crossed, spaces otherwise -- bash's indent().
 */

static void indent(int from, int to)
{
  while (from < to)
    {
      if (to / TABSIZE > from / TABSIZE)
        {
          fputc('\t', stderr);
          from += TABSIZE - from % TABSIZE;
        }
      else
        {
          fputc(' ', stderr);
          from++;
        }
    }
}

void vs_select_menu(char *const *items, int n)
{
  int indices_len = digitlen(n);
  int max_item = 0;
  int max_elem_len;
  int cols;
  int rows;
  int first_indices_len;
  int row;
  int i;

  for (i = 0; i < n; i++)
    {
      int w = (int)vs_mb_count(items[i]);

      if (w > max_item)
        {
          max_item = w;
        }
    }

  max_elem_len = max_item + indices_len + 2 + 2;     /* item + "n) " + a gap */

  cols = max_elem_len > 0 ? term_columns() / max_elem_len : 1;
  if (cols == 0)
    {
      cols = 1;
    }

  rows = n / cols + (n % cols != 0);
  cols = n / rows + (n % rows != 0);
  if (rows == 1)
    {
      rows = cols;                     /* never one wide row: one item per line instead */
      cols = 1;
    }

  first_indices_len = digitlen(rows);

  for (row = 0; row < rows; row++)
    {
      int ind = row;
      int pos = 0;

      for (; ; )
        {
          int width = pos == 0 ? first_indices_len : indices_len;
          int elem_len;

          fprintf(stderr, "%*d) %s", width, ind + 1, items[ind]);
          elem_len = width + 2 + (int)vs_mb_count(items[ind]);
          ind += rows;
          if (ind >= n)
            {
              break;
            }

          indent(pos + elem_len, pos + max_elem_len);
          pos += max_elem_len;
        }

      fputc('\n', stderr);
    }
}

int vs_select_parse(const char *reply, int n)
{
  char *end;
  long long v;

  if (reply[0] == '\0')
    {
      return -1;
    }

  errno = 0;
  v = strtoll(reply, &end, 10);
  if (end == reply)
    {
      return 0;
    }

  while (*end == ' ' || *end == '\t' || *end == '\n')
    {
      end++;
    }

  if (*end != '\0' || errno == ERANGE || v < 1 || v > n)
    {
      return 0;
    }

  return (int)v;
}
