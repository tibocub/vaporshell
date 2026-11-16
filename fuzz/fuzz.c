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
 */

#include <nuttx/config.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "exec.h"
#include "expand.h"
#include "history.h"
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

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
  (void)argc;
  shell_init((*argv)[0]);
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

  switch (data[0] % 5)
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

      default:
        free(vash_hist_expand(s));
        break;
    }

  free(s);
  return 0;
}
