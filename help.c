/*
 * help.c -- `help`: lists the builtins straight from the builtin table,
 * so it can never drift from what the shell actually has. On NuttX it
 * also lists the commands reachable through tbx and the programs in /bin.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>

#ifndef VAPORSHELL_POSIX
#  include <dirent.h>
#  include <errno.h>
#endif

#include "vaporshell.h"
#include "exec.h"

#ifndef VAPORSHELL_POSIX
extern const char *const g_tbx_commands[];   /* dispatch.c */
#endif

int bi_help(int argc, char **argv)
{
  const struct builtin_s *b;
  int i;

  if (argc > 1)
    {
      int status = 0;

      for (i = 1; i < argc; i++)
        {
          b = builtin_find(argv[i]);
          if (b == NULL)
            {
              vs_err("help: no help for `%s'", argv[i]);
              status = 1;
            }
          else
            {
              printf("%s: %s\n", b->name, b->help);
            }
        }

      return status;
    }

  puts("vaporshell builtins (* = POSIX special builtin):");
  for (b = g_builtins; b->name != NULL; b++)
    {
      printf("  %-9s%s %s\n", b->name, b->special ? "*" : " ", b->help);
    }

#ifndef VAPORSHELL_POSIX
  {
    DIR *dir;
    struct dirent *entry;

    printf("\ncommands (via tbx):\n ");
    for (i = 0; g_tbx_commands[i] != NULL; i++)
      {
        printf(" %s", g_tbx_commands[i]);
      }

    printf("\n\nother programs (/bin):\n ");
    dir = opendir("/bin");
    if (dir == NULL)
      {
        printf(" (couldn't read /bin: %s)", strerror(errno));
      }
    else
      {
        while ((entry = readdir(dir)) != NULL)
          {
            if (strcmp(entry->d_name, ".") != 0 &&
                strcmp(entry->d_name, "..") != 0)
              {
                printf(" %s", entry->d_name);
              }
          }

        closedir(dir);
      }

    printf("\n");
  }
#endif

  return 0;
}
