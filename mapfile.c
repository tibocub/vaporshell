/*
 * mapfile.c -- mapfile / readarray: read lines from a file descriptor into an
 * indexed array.
 *
 *   mapfile [-d delim] [-n count] [-O origin] [-s count] [-t] [-u fd]
 *           [-C callback] [-c quantum] [array]
 *
 * The array is MAPFILE unless one is named, and is emptied first unless -O
 * says where to start. -t drops the delimiter from each element. -s skips
 * lines before storing any; -n stops after storing that many. -C runs a
 * callback after every -c lines (5000 by default) with the index the element
 * is about to get and its text appended, before the element is stored.
 */

#include <nuttx/config.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vaporshell.h"
#include "exec.h"

#define MAPFILE_QUANTUM 5000

/* Input, one byte at a time. With a count to honour (-n) the shell must not
 * read past the last line it takes, so it cannot buffer; otherwise it reads
 * to the end anyway and buffers freely.
 */

struct rd_s
{
  int fd;
  bool exact;                        /* no read-ahead */
  unsigned char buf[4096];
  size_t pos;
  size_t len;
};

static int rd_getc(struct rd_s *r)
{
  if (r->exact)
    {
      unsigned char c;

      return read(r->fd, &c, 1) == 1 ? c : -1;
    }

  if (r->pos == r->len)
    {
      ssize_t n = read(r->fd, r->buf, sizeof(r->buf));

      if (n <= 0)
        {
          return -1;
        }

      r->pos = 0;
      r->len = (size_t)n;
    }

  return r->buf[r->pos++];
}

/* A non-negative number, all of it. */

static bool parse_count(const char *s, long *out)
{
  char *end;
  long v;

  errno = 0;
  v = strtol(s, &end, 10);
  if (s[0] == '\0' || *end != '\0' || errno != 0 || v < 0)
    {
      return false;
    }

  *out = v;
  return true;
}

/* 'text' in single quotes, as one word. */

static char *sq_quote(const char *text)
{
  struct sbuf_s q;
  const char *p;

  sb_init(&q);
  sb_addc(&q, '\'');
  for (p = text; *p != '\0'; p++)
    {
      if (*p == '\'')
        {
          sb_adds(&q, "'\\''");
        }
      else
        {
          sb_addc(&q, *p);
        }
    }

  sb_addc(&q, '\'');
  return q.s;
}

/* Runs `callback index 'line'` in the shell; returns false if the shell is
 * unwinding (the callback did `exit`, say) and we should stop.
 */

static bool run_callback(const char *callback, long idx, const char *line)
{
  struct sbuf_s cmd;
  char *q = sq_quote(line);
  char num[24];

  snprintf(num, sizeof(num), " %ld ", idx);
  sb_init(&cmd);
  sb_adds(&cmd, callback);
  sb_adds(&cmd, num);
  sb_adds(&cmd, q);
  g_sh.syntax_error = false;
  run_string(cmd.s, cmd.len);
  sb_free(&cmd);
  free(q);
  return g_sh.unwind == UW_NONE;
}

int bi_mapfile(int argc, char **argv)
{
  const char *word = argv[0];
  const char *name = "MAPFILE";
  const char *callback = NULL;
  int delim = '\n';
  int fd = STDIN_FILENO;
  long count = 0;                    /* 0: no limit */
  long origin = 0;
  long skip = 0;
  long quantum = MAPFILE_QUANTUM;
  bool strip = false;
  bool clear = true;
  int i = 1;
  struct rd_s rd;
  struct sbuf_s rec;
  struct var_s *v;
  long idx;
  long seen = 0;
  long skipped = 0;
  long stored = 0;
  int status = 0;

  for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++)
    {
      const char *c;

      if (strcmp(argv[i], "--") == 0)
        {
          i++;
          break;
        }

      for (c = argv[i] + 1; *c != '\0'; c++)
        {
          char opt = *c;
          const char *val;

          if (opt == 't')
            {
              strip = true;
              continue;
            }

          if (strchr("dnOsuCc", opt) == NULL)
            {
              vs_err("%s: -%c: invalid option", word, opt);
              vs_err("%s: usage: %s [-d delim] [-n count] [-O origin] [-s count] [-t] "
                     "[-u fd] [-C callback] [-c quantum] [array]", word, word);
              return 2;
            }

          if (c[1] != '\0')
            {
              val = c + 1;
              c += strlen(c) - 1;
            }
          else if (i + 1 < argc)
            {
              val = argv[++i];
            }
          else
            {
              vs_err("%s: -%c: option requires an argument", word, opt);
              return 2;
            }

          switch (opt)
            {
              case 'd':
                delim = val[0] != '\0' ? (unsigned char)val[0] : 0;
                break;

              case 'n':
                if (!parse_count(val, &count))
                  {
                    vs_err("%s: %s: invalid line count", word, val);
                    return 1;
                  }

                break;

              case 's':
                if (!parse_count(val, &skip))
                  {
                    vs_err("%s: %s: invalid line count", word, val);
                    return 1;
                  }

                break;

              case 'O':
                if (!parse_count(val, &origin))
                  {
                    vs_err("%s: %s: invalid array origin", word, val);
                    return 1;
                  }

                clear = false;
                break;

              case 'u':
                {
                  long n;

                  if (!parse_count(val, &n) || n > 1023)
                    {
                      vs_err("%s: %s: invalid file descriptor specification", word, val);
                      return 1;
                    }

                  fd = (int)n;
                }

                break;

              case 'C':
                callback = val;
                break;

              default:
                if (!parse_count(val, &quantum) || quantum == 0)
                  {
                    vs_err("%s: %s: invalid callback quantum", word, val);
                    return 1;
                  }

                break;
            }
        }
    }

  if (i < argc)
    {
      name = argv[i];
    }

  if (!is_valid_name(name, strlen(name)))
    {
      vs_err("%s: `%s': not a valid identifier", word, name);
      return 1;
    }

  if (var_is_assoc(name))
    {
      vs_err("%s: %s: not an indexed array", word, name);
      return 1;
    }

  v = var_lookup(name);
  if (v != NULL && (v->flags & VF_READONLY) != 0)
    {
      vs_err("%s: readonly variable", name);
      return 1;
    }

  if (fcntl(fd, F_GETFL) < 0)
    {
      vs_err("%s: %d: invalid file descriptor: %s", word, fd, strerror(errno));
      return 1;
    }

  /* From here on the array is ours. */

  if (clear)
    {
      if (var_array_replace(name, arr_new()) != 0)
        {
          return 1;
        }
    }
  else if (var_array(name, true) == NULL)
    {
      return 1;
    }

  rd.fd = fd;
  rd.exact = count > 0;
  rd.pos = 0;
  rd.len = 0;
  idx = origin;
  sb_init(&rec);

  while (count == 0 || stored < count)
    {
      bool hit = false;
      bool eof = false;

      rec.len = 0;
      if (rec.s != NULL)
        {
          rec.s[0] = '\0';
        }

      for (; ; )
        {
          int ch = rd_getc(&rd);

          if (ch < 0)
            {
              eof = true;
              break;
            }

          if (ch == delim)
            {
              hit = true;
              break;
            }

          if (ch != 0)                 /* a NUL byte cannot live in a shell string */
            {
              sb_addc(&rec, (char)ch);
            }
        }

      if (eof && rec.len == 0)
        {
          break;                       /* no partial line: done */
        }

      if (hit && !strip && delim != 0)
        {
          sb_addc(&rec, (char)delim);
        }

      if (skipped < skip)
        {
          skipped++;
        }
      else
        {
          const char *text = rec.len > 0 ? rec.s : "";

          seen++;
          if (callback != NULL && seen % quantum == 0 && !run_callback(callback, idx, text))
            {
              status = g_sh.last_status;
              break;
            }

          var_elem_set(name, idx, text);
          idx++;
          stored++;
        }

      if (eof)
        {
          break;
        }
    }

  sb_free(&rec);
  return status;
}
