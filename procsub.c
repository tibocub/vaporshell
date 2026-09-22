/*
 * procsub.c -- <(cmd) and >(cmd) (bash's process substitution).
 *
 * bash gives each a real, but transient, file descriptor: <(cmd) is a pipe
 * fed by a background copy of the shell running cmd, so the reader sees its
 * output as it is produced; >(cmd) is the reverse, a background cmd reading
 * whatever the foreground command writes.
 *
 * This shell has no background-execution model to build that on (NuttX has
 * no fork at all, and even command substitution here runs to completion
 * before its result is used, in-process or not). So both are approximated:
 * <(cmd) runs cmd to completion first (through the same machinery command
 * substitution uses) and its captured output becomes a real temp file, which
 * is what the expansion yields instead of a pipe path. >(cmd) hands out an
 * empty temp file for the foreground command to write into, and once that
 * command has finished, cmd is run with that file as its input. Either way
 * the end result -- what ends up read, or what cmd is given -- matches bash;
 * only true concurrency (interleaved timing, or a producer that depends on a
 * consumer already reading) is not.
 *
 * A path is valid until procsub_drain() removes it: exec_node() calls
 * procsub_mark() before running a node and procsub_drain() with that mark
 * after, so a substitution's file outlives the single command that used it
 * but no longer.
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

struct procsub_s
{
  char *path;
  char *cmd;                 /* NULL for <(...): nothing left to run */
};

static char *make_temp(void)
{
  const char *tmp = var_get("TMPDIR");
  char *path;
  int fd;

  if (tmp == NULL || tmp[0] == '\0')
    {
      tmp = "/tmp";
    }

  path = vs_xmalloc(strlen(tmp) + sizeof("/vs-procsub-XXXXXX") + 1);
  sprintf(path, "%s/vs-procsub-XXXXXX", tmp);
  fd = mkstemp(path);
  if (fd < 0)
    {
      vs_err("%s: %s", path, strerror(errno));
      free(path);
      return NULL;
    }

  close(fd);
  return path;
}

/* cmd (cmdlen bytes, not NUL-terminated) is <(cmd) or, if is_output, >(cmd).
 * Returns a path good until the enclosing command finishes, or NULL on error
 * (nothing left registered in that case).
 */

char *procsub_new(const char *cmd, size_t cmdlen, bool is_output)
{
  char *path = make_temp();
  struct procsub_s *e;

  if (path == NULL)
    {
      return NULL;
    }

  if (!is_output)
    {
      char *captured = run_capture_raw(cmd, cmdlen);
      FILE *f = fopen(path, "w");

      if (f != NULL)
        {
          fwrite(captured, 1, strlen(captured), f);
          fclose(f);
        }

      free(captured);
    }

  if (g_sh.procsub_n == g_sh.procsub_cap)
    {
      g_sh.procsub_cap = g_sh.procsub_cap > 0 ? g_sh.procsub_cap * 2 : 4;
      g_sh.procsub = vs_xrealloc(g_sh.procsub, g_sh.procsub_cap * sizeof(*g_sh.procsub));
    }

  e = &g_sh.procsub[g_sh.procsub_n++];
  e->path = vs_xstrdup(path);
  e->cmd = is_output ? vs_xstrndup(cmd, cmdlen) : NULL;
  return path;
}

size_t procsub_mark(void)
{
  return g_sh.procsub_n;
}

/* Runs every >(...) registered since 'from' with its file as stdin, then
 * removes every entry (in and out alike) since 'from'.
 */

void procsub_drain(size_t from)
{
  size_t i;

  if (from >= g_sh.procsub_n)
    {
      return;
    }

  for (i = from; i < g_sh.procsub_n; i++)
    {
      struct procsub_s *e = &g_sh.procsub[i];

      if (e->cmd != NULL)
        {
          int fd = open(e->path, O_RDONLY);

          if (fd >= 0)
            {
              int saved = dup(STDIN_FILENO);

              fflush(NULL);
              dup2(fd, STDIN_FILENO);
              close(fd);
              run_string(e->cmd, strlen(e->cmd));
              if (saved >= 0)
                {
                  dup2(saved, STDIN_FILENO);
                  close(saved);
                }
            }
        }

      unlink(e->path);
      free(e->path);
      free(e->cmd);
    }

  g_sh.procsub_n = from;
}
