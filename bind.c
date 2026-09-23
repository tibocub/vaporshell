/*
 * bind.c -- readline key bindings. There is no line editor in this shell to
 * bind a key in, so, like bash's own `bind` outside a real terminal, this
 * warns and does nothing rather than pretending a binding took effect.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>

#include "vaporshell.h"
#include "exec.h"

int bi_bind(int argc, char **argv)
{
  int i;
  bool query_only = false;

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "-P") == 0 ||
          strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "-L") == 0 ||
          strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "-S") == 0 ||
          strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-V") == 0)
        {
          query_only = true;         /* list something: nothing to list, but not an error */
        }
    }

  vs_err("bind: warning: line editing not enabled");
  return query_only ? 0 : 0;
}
