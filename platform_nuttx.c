/*
 * platform_nuttx.c -- the platform layer for NuttX: no fork(), programs
 * are started with posix_spawnp on the bare name (NuttX resolves its
 * builtin apps itself), and command substitution runs a child
 * "vaporshell -c" instead of forking.
 *
 * NOT COMPILED OR RUN by the author of this change (no NuttX tree was
 * available): it is a direct port of what the previous exec.c, pipeline.c
 * and subst.c did, kept deliberately close to them.
 */

#include <nuttx/config.h>
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "vaporshell.h"
#include "platform.h"

bool is_tbx_command(const char *name);        /* dispatch.c */

bool vs_plat_interactive(void)
{
  return true;
}

bool vs_plat_have_fork(void)
{
  return false;
}

pid_t vs_plat_fork(void)
{
  errno = ENOSYS;
  return -1;
}

bool vs_plat_export_all(void)
{
  return true;
}

char *vs_plat_find_command(const char *name, const char *path_var, int *err)
{
  (void)path_var;
  (void)err;
  return vs_xstrdup(name);
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

  ret = posix_spawnp(pid, path, &actions, NULL, argv, envp);

  /* Fall back to the tbx multicall binary only when the name isn't a real
   * program: an installed program of the same name always wins.
   */

  if (ret == ENOENT && is_tbx_command(argv[0]))
    {
      char *tbx_argv[64];
      int n = 0;

      tbx_argv[n++] = "tbx";
      while (argv[n - 1] != NULL && n < 63)
        {
          tbx_argv[n] = argv[n - 1];
          n++;
        }

      tbx_argv[n] = NULL;
      ret = posix_spawnp(pid, "tbx", &actions, NULL, tbx_argv, envp);
    }

  posix_spawn_file_actions_destroy(&actions);
  return ret;
}

int vs_plat_exec(const char *path, char *const argv[], char *const envp[])
{
  (void)path;
  (void)argv;
  (void)envp;
  errno = ENOSYS;
  return -1;
}

int vs_plat_dup_high(int fd)
{
  return dup(fd);
}

char *vs_plat_capture_via_self(const char *text, int *status)
{
  int pipefd[2];
  posix_spawn_file_actions_t actions;
  pid_t pid;
  char *argv[4];
  char **envp;
  int ret;
  int wstatus = 0;
  struct sbuf_s out;

  *status = 1;
  if (pipe(pipefd) != 0)
    {
      return NULL;
    }

  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addclose(&actions, pipefd[0]);
  posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&actions, pipefd[1]);

  argv[0] = (char *)g_sh.self;
  argv[1] = "-c";
  argv[2] = (char *)text;
  argv[3] = NULL;

  envp = var_build_env();
  ret = posix_spawnp(&pid, g_sh.self, &actions, NULL, argv, envp);
  env_free(envp);
  posix_spawn_file_actions_destroy(&actions);
  close(pipefd[1]);

  if (ret != 0)
    {
      close(pipefd[0]);
      return NULL;
    }

  sb_init(&out);
  for (; ; )
    {
      char buf[256];
      ssize_t n = read(pipefd[0], buf, sizeof(buf));

      if (n <= 0)
        {
          break;
        }

      sb_addn(&out, buf, (size_t)n);
    }

  close(pipefd[0]);
  if (waitpid(pid, &wstatus, 0) >= 0)
    {
      *status = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 1;
    }

  return sb_take(&out);
}
