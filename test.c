/*
 * test.c -- the `test` / `[` builtin (POSIX test(1) with the common
 * extensions -o/-a, parentheses, -nt/-ot/-ef). Exit status: 0 true,
 * 1 false, 2 usage error.
 *
 * Being a builtin means `[ ... ]` costs no process, and works when PATH
 * is empty or unusual.
 */

#include <nuttx/config.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vaporshell.h"
#include "exec.h"
#include "mode.h"

struct tst_s
{
  char **v;
  int n;
  int i;
  bool err;
};

static bool t_or(struct tst_s *t);

static void t_fail(struct tst_s *t, const char *msg)
{
  if (!t->err)
    {
      vs_err("test: %s", msg);
      t->err = true;
    }
}

static bool is_binary(const char *s)
{
  static const char *const ops[] =
  {
    "=", "==", "!=", "<", ">", "-eq", "-ne", "-lt", "-le", "-gt", "-ge",
    "-nt", "-ot", "-ef"
  };
  size_t i;

  for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++)
    {
      if (strcmp(s, ops[i]) == 0)
        {
          /* == is a bash extension; POSIX test only has = . */

          return strcmp(s, "==") != 0 || vs_feat(VF_TEST_EXT);
        }
    }

  return false;
}

static bool is_unary(const char *s)
{
  return s[0] == '-' && s[1] != '\0' && s[2] == '\0' &&
         strchr("bcdefghLnprsSuwxzOGkt", s[1]) != NULL;
}

static bool to_long(struct tst_s *t, const char *s, long *out)
{
  char *end;

  errno = 0;
  *out = strtol(s, &end, 10);
  if (*s == '\0' || *end != '\0' || errno != 0)
    {
      t_fail(t, "integer expression expected");
      return false;
    }

  return true;
}

static bool file_test(struct tst_s *t, char op, const char *path)
{
  struct stat st;

  (void)t;
  if (op == 'L' || op == 'h')
    {
      return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
    }

  if (op == 'r')
    {
      return access(path, R_OK) == 0;
    }

  if (op == 'w')
    {
      return access(path, W_OK) == 0;
    }

  if (op == 'x')
    {
      return access(path, X_OK) == 0;
    }

  if (stat(path, &st) != 0)
    {
      return false;
    }

  switch (op)
    {
      case 'e': return true;
      case 'f': return S_ISREG(st.st_mode);
      case 'd': return S_ISDIR(st.st_mode);
      case 'b': return S_ISBLK(st.st_mode);
      case 'c': return S_ISCHR(st.st_mode);
      case 'p': return S_ISFIFO(st.st_mode);
      case 'S': return S_ISSOCK(st.st_mode);
      case 's': return st.st_size > 0;
      case 'g': return (st.st_mode & S_ISGID) != 0;
      case 'u': return (st.st_mode & S_ISUID) != 0;
#ifdef S_ISVTX
      case 'k': return (st.st_mode & S_ISVTX) != 0;
#endif
      case 'O': return st.st_uid == geteuid();
      case 'G': return st.st_gid == getegid();
      default:  return false;
    }
}

static bool binary(struct tst_s *t, const char *a, const char *op, const char *b)
{
  long x;
  long y;

  if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0)
    {
      return strcmp(a, b) == 0;
    }

  if (strcmp(op, "!=") == 0)
    {
      return strcmp(a, b) != 0;
    }

  if (strcmp(op, "<") == 0)
    {
      return strcmp(a, b) < 0;
    }

  if (strcmp(op, ">") == 0)
    {
      return strcmp(a, b) > 0;
    }

  if (strcmp(op, "-nt") == 0 || strcmp(op, "-ot") == 0 ||
      strcmp(op, "-ef") == 0)
    {
      struct stat sa;
      struct stat sb;
      bool ha = stat(a, &sa) == 0;
      bool hb = stat(b, &sb) == 0;

      if (op[1] == 'e')
        {
          return ha && hb && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
        }

      if (op[1] == 'n')
        {
          return ha && (!hb || sa.st_mtime > sb.st_mtime);
        }

      return hb && (!ha || sa.st_mtime < sb.st_mtime);
    }

  if (!to_long(t, a, &x) || !to_long(t, b, &y))
    {
      return false;
    }

  if (strcmp(op, "-eq") == 0) return x == y;
  if (strcmp(op, "-ne") == 0) return x != y;
  if (strcmp(op, "-lt") == 0) return x < y;
  if (strcmp(op, "-le") == 0) return x <= y;
  if (strcmp(op, "-gt") == 0) return x > y;
  return x >= y;
}

static bool t_primary(struct tst_s *t)
{
  const char *a;
  int left = t->n - t->i;

  if (left <= 0)
    {
      t_fail(t, "argument expected");
      return false;
    }

  a = t->v[t->i];

  /* "a OP b": binary wins if the middle word is a binary operator (so
   * [ "(" = "(" ] compares strings instead of opening a group).
   */

  if (left >= 3 && is_binary(t->v[t->i + 1]))
    {
      const char *b = t->v[t->i + 2];
      const char *op = t->v[t->i + 1];

      t->i += 3;
      return binary(t, a, op, b);
    }

  if (strcmp(a, "(") == 0 && left >= 2)
    {
      bool r;

      t->i++;
      r = t_or(t);
      if (t->i < t->n && strcmp(t->v[t->i], ")") == 0)
        {
          t->i++;
        }
      else
        {
          t_fail(t, "missing `)'");
        }

      return r;
    }

  if (left >= 2 && is_unary(a))
    {
      const char *b = t->v[t->i + 1];

      t->i += 2;
      switch (a[1])
        {
          case 'n': return b[0] != '\0';
          case 'z': return b[0] == '\0';
          case 't':
            {
              long fd;

              return to_long(t, b, &fd) && isatty((int)fd) != 0;
            }

          default:
            return file_test(t, a[1], b);
        }
    }

  t->i++;
  return a[0] != '\0';
}

static bool t_not(struct tst_s *t)
{
  if (t->i < t->n && strcmp(t->v[t->i], "!") == 0 && t->n - t->i >= 2 &&
      !(t->n - t->i >= 3 && is_binary(t->v[t->i + 1])))
    {
      t->i++;
      return !t_not(t);
    }

  return t_primary(t);
}

static bool t_and(struct tst_s *t)
{
  bool r = t_not(t);

  while (t->i < t->n && strcmp(t->v[t->i], "-a") == 0)
    {
      t->i++;
      r = t_not(t) && r;
    }

  return r;
}

static bool t_or(struct tst_s *t)
{
  bool r = t_and(t);

  while (t->i < t->n && strcmp(t->v[t->i], "-o") == 0)
    {
      t->i++;
      r = t_and(t) || r;
    }

  return r;
}

static int run_test(char **v, int n)
{
  struct tst_s t;
  bool r;

  if (n == 0)
    {
      return 1;
    }

  t.v = v;
  t.n = n;
  t.i = 0;
  t.err = false;
  r = t_or(&t);
  if (!t.err && t.i < t.n)
    {
      t_fail(&t, "too many arguments");
    }

  return t.err ? 2 : (r ? 0 : 1);
}

int bi_test(int argc, char **argv)
{
  return run_test(argv + 1, argc - 1);
}

int bi_bracket(int argc, char **argv)
{
  if (argc < 2 || strcmp(argv[argc - 1], "]") != 0)
    {
      vs_err("[: missing `]'");
      return 2;
    }

  return run_test(argv + 1, argc - 2);
}
