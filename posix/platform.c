/*
 * posix/platform.c -- the platform layer for the standalone build
 * (Linux, macOS, BSD): real fork(), PATH search, posix_spawn.
 */

#include <nuttx/config.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vaporshell.h"
#include "platform.h"

bool vs_plat_interactive(void)
{
  return isatty(STDIN_FILENO) != 0;
}

bool vs_plat_have_fork(void)
{
  return true;
}

pid_t vs_plat_fork(void)
{
  fflush(NULL);                 /* or buffered output would appear twice */
  return fork();
}

bool vs_plat_export_all(void)
{
  return false;
}

bool vs_plat_external_fallback(const char *name)
{
  (void)name;
  return false;                 /* pipeline stages fork instead */
}

static bool is_executable_file(const char *path, int *err)
{
  struct stat st;

  if (stat(path, &st) != 0)
    {
      return false;
    }

  if (S_ISDIR(st.st_mode) || access(path, X_OK) != 0)
    {
      *err = EACCES;
      return false;
    }

  return true;
}

char *vs_plat_find_command(const char *name, const char *path_var, int *err)
{
  struct sbuf_s cand;
  const char *path = path_var != NULL ? path_var : "/usr/bin:/bin";

  *err = ENOENT;
  if (name[0] == '\0')
    {
      return NULL;
    }

  if (strchr(name, '/') != NULL)
    {
      return is_executable_file(name, err) ? vs_xstrdup(name) : NULL;
    }

  sb_init(&cand);
  for (; ; )
    {
      size_t len = strcspn(path, ":");

      cand.len = 0;
      if (len > 0)
        {
          sb_addn(&cand, path, len);
          sb_addc(&cand, '/');
        }

      sb_adds(&cand, name);
      if (is_executable_file(cand.s, err))
        {
          return sb_take(&cand);
        }

      if (path[len] == '\0')
        {
          break;
        }

      path += len + 1;
    }

  sb_free(&cand);
  return NULL;
}

int vs_plat_spawn(const char *path, char *const argv[], char *const envp[],
                  int in_fd, int out_fd, const int *close_fds, int nclose,
                  pid_t *pid)
{
  posix_spawn_file_actions_t actions;
  int ret;
  int i;

  posix_spawn_file_actions_init(&actions);
  if (in_fd >= 0)
    {
      posix_spawn_file_actions_adddup2(&actions, in_fd, STDIN_FILENO);
    }

  if (out_fd >= 0)
    {
      posix_spawn_file_actions_adddup2(&actions, out_fd, STDOUT_FILENO);
    }

  for (i = 0; i < nclose; i++)
    {
      posix_spawn_file_actions_addclose(&actions, close_fds[i]);
    }

  ret = posix_spawn(pid, path, &actions, NULL, argv, envp);
  posix_spawn_file_actions_destroy(&actions);
  return ret;
}

int vs_plat_exec(const char *path, char *const argv[], char *const envp[])
{
  return execve(path, argv, envp);
}

int vs_plat_dup_high(int fd)
{
  return fcntl(fd, F_DUPFD_CLOEXEC, 10);
}

char *vs_plat_capture_via_self(const char *text, int *status)
{
  (void)text;
  *status = 1;
  return NULL;                  /* never used: this platform forks */
}
