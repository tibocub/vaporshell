/*
 * bind.c -- readline key bindings. Accepted and ignored for now, so a bash rc
 * file that calls `bind` still loads; the editor's keys are the table in
 * ledit.c. A real `bind` is planned (docs/interactive.md).
 */

#include <nuttx/config.h>

#include "vaporshell.h"
#include "exec.h"

int bi_bind(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  return 0;
}
