/*
 * fuzz.c -- libFuzzer harness for the parts of vaporshell that take untrusted
 * text and never run it. The first input byte picks the target, so one binary
 * and one corpus cover them all (make fuzz; see docs/security.md).
 *
 *   0  parser     lexer + parser over a script (nothing is executed)
 *   1  arith      $(( )) evaluator (inputs with '$' or '`' are skipped: those
 *                 would expand, and expansion can run commands)
 *   2  pattern    glob matching, input split at the first '\n' into pattern/string
 *   3  brace      brace expansion
 *   4  history    !-expansion of a line against an empty history
 *   5  expand     word expansion ("$x" ${x//a/b} ~ globs...), skipped when the text could
 *                 run a command: "$(", "`", "<(", ">("
 *   6  printf     the printf builtin, always `-v var` (formats into a variable)
 *   7  test       the test/[ builtin on the input's lines as arguments
 *   8  highlight  the editor's syntax highlighter
 *   9  complete   Tab completion of the input as a command line (specs, files, variables)
 *  10  bind       the `bind` builtin: key sequences and readline commands, one argument per line
 *  11  run        runs the input as a script, sandboxed: externals cannot be found (PATH is
 *                 read-only and points nowhere), and inputs that could leave the sandbox are
 *                 skipped (see runnable(): paths, redirections, loops, functions, exec, ...)
 */

#include <nuttx/config.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "exec.h"
#include "expand.h"
#include "complete.h"
#include "history.h"
#include "ledit.h"
#include "parse.h"

static void fuzz_parser(const char *s, size_t n)
{
  struct parser_s p;
  int guard;

  parser_init_mem(&p, s, n);
  for (guard = 0; guard < 10000; guard++)
    {
      struct arena_s *arena = NULL;
      struct node_s *node = NULL;
      enum parse_result_e r = parse_command_line(&p, &arena, &node);

      if (arena != NULL)
        {
          arena_release(arena);
        }

      if (r == PARSE_EOF)
        {
          break;
        }

      if (r == PARSE_ERR)
        {
          parser_discard(&p);
          break;
        }
    }

  parser_free(&p);
}

void vash_fuzz_highlight(const char *s, size_t len);      /* ledit.c, with -DVASH_FUZZ */

static void cand(void *ctx, char *name, char *desc, char suffix)
{
  (void)ctx;
  (void)suffix;
  free(name);
  free(desc);
}

/* Can this script run without touching anything outside the process? With no way to find a
 * program (PATH is read-only and empty of programs), what is left is the shell's own builtins
 * and expansions; these words and characters would still reach out or never come back.
 */

static bool runnable(const char *s, size_t n)
{
  static const char *const bad[] =
  {
    "exit", "exec", "kill", "trap", "read", "wait", "cd", "pushd", "popd", "source", "ulimit", "umask",
    "enable", "command", "builtin", "while", "until", "for", "select", "fg", "bg", "disown", "suspend",
    "logout", "mapfile", "readarray", "coproc", "history", "fc", "hash", "()", "function", "set -e",
    "set -u", "errexit", "nounset", NULL
  };

  int i;

  if (strpbrk(s, "/&<>~\\") != NULL || strlen(s) > 256)
    {
      return false;
    }

  for (i = 0; bad[i] != NULL; i++)
    {
      if (strstr(s, bad[i]) != NULL)
        {
          return false;
        }
    }

  return true;
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
  int nul = open("/dev/null", O_WRONLY);

  (void)argc;
  setenv("PATH", "/nonexistent", 1);
  shell_init((*argv)[0]);          /* globs and tests look at the current directory: run it in a scratch one */
  run_string("readonly PATH", 13);
  if (nul >= 0)
    {
      dup2(nul, 1);                /* what the scripts print is not interesting */
    }

  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  char *s;
  char *nl;

  if (size < 1 || size > 4096)
    {
      return 0;
    }

  s = malloc(size);                          /* NUL-terminated, exactly sized: ASan sees overreads */
  memcpy(s, data + 1, size - 1);
  s[size - 1] = '\0';

  switch (data[0] % 12)
    {
      case 0:
        fuzz_parser(s, size - 1);
        break;

      case 1:
        if (strpbrk(s, "$`") == NULL)
          {
            long v;

            arith_eval(s, &v);
          }

        break;

      case 2:
        nl = strchr(s, '\n');
        if (nl != NULL)
          {
            *nl = '\0';
            pat_match(s, NULL, strlen(s), nl + 1);
          }

        break;

      case 3:
        if (size < 200)                     /* a long word makes the 100000-result cap slow under ASan: not a bug */
        {
          struct fieldv_s f;

          memset(&f, 0, sizeof(f));
          vash_brace_expand(s, &f);
          fv_free(&f);
        }

        break;

      case 4:
        free(vash_hist_expand(s));
        break;

      case 5:
        if (size < 128 && strstr(s, "$(") == NULL && strchr(s, '`') == NULL && strstr(s, "<(") == NULL &&
            strstr(s, ">(") == NULL)
          {
            struct fieldv_s f;

            free(expand_word_str(s));
            memset(&f, 0, sizeof(f));
            expand_token_fields(s, &f);
            fv_free(&f);
          }

        break;

      case 6:
      case 7:
        {
          char *av[10] = { data[0] % 12 == 6 ? "printf" : "test", NULL };
          int ac = 1;
          char *t = s;

          if (data[0] % 12 == 6)
            {
              av[ac++] = "-v";
              av[ac++] = "fuzzvar";
            }

          while (t != NULL && ac < 9)
            {
              av[ac++] = t;
              t = strchr(t, '\n');
              if (t != NULL)
                {
                  *t++ = '\0';
                }
            }

          av[ac] = NULL;
          if (data[0] % 12 == 6)
            {
              bi_printf(ac, av);
            }
          else
            {
              bi_test(ac, av);
            }
        }

        break;

      case 8:
        vash_fuzz_highlight(s, size - 1);
        break;

      case 9:
        {
          char ins[1024];
          int del;
          size_t skip;

          vash_commands_refresh();
          vash_complete(s, (int)(size - 1), ins, sizeof(ins), &del, &skip, cand, NULL);
        }

        break;

      case 10:
        {
          char *av[10] = { "bind", NULL };
          int ac = 1;
          char *t = s;

          while (t != NULL && ac < 9)
            {
              av[ac++] = t;
              t = strchr(t, '\n');
              if (t != NULL)
                {
                  *t++ = '\0';
                }
            }

          av[ac] = NULL;
          bi_bind(ac, av);
        }

        break;

      case 11:
        if (runnable(s, size - 1))
          {
            run_string(s, size - 1);
          }

        break;

      default:
        break;
    }

  free(s);
  return 0;
}
