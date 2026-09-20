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
#include <fcntl.h>
#include <sched.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "vaporshell.h"
#include "mode.h"
#include "platform.h"

bool is_tbx_command(const char *name);        /* dispatch.c */

/* ---- Per-task shell state --------------------------------------------------
 *
 * In a flat NuttX build every instance of this app shares its global data,
 * so state is looked up by task id. Entries are only ever written by the
 * task that owns them (under sched_lock, which also protects the free
 * slot search), and a task only ever reads its own -- so lookups need no
 * lock.
 */

#define VS_MAX_INSTANCES 16

static struct
{
  pid_t pid;                    /* 0: free */
  struct shell_s *sh;
} g_instances[VS_MAX_INSTANCES];

struct shell_s *vs_plat_state_create(void)
{
  struct shell_s *sh = vs_xmalloc(sizeof(*sh));
  pid_t me = getpid();
  int i;

  memset(sh, 0, sizeof(*sh));
  sched_lock();
  for (i = 0; i < VS_MAX_INSTANCES; i++)
    {
      if (g_instances[i].pid == 0)
        {
          g_instances[i].sh = sh;
          g_instances[i].pid = me;
          sched_unlock();
          return sh;
        }
    }

  sched_unlock();
  free(sh);
  fputs("vaporshell: too many shell instances\n", stderr);
  exit(1);
}

struct shell_s *vs_state(void)
{
  pid_t me = getpid();
  int i;

  for (i = 0; i < VS_MAX_INSTANCES; i++)
    {
      if (g_instances[i].pid == me)
        {
          return g_instances[i].sh;
        }
    }

  return vs_plat_state_create();       /* a task we have not seen: start fresh */
}

void vs_plat_state_destroy(void)
{
  pid_t me = getpid();
  int i;

  for (i = 0; i < VS_MAX_INSTANCES; i++)
    {
      if (g_instances[i].pid == me)
        {
          struct shell_s *sh = g_instances[i].sh;

          sched_lock();
          g_instances[i].pid = 0;
          g_instances[i].sh = NULL;
          sched_unlock();
          free(sh);
          return;
        }
    }
}

/* ---- Paths ------------------------------------------------------------------- */

static bool has_dot_component(const char *path)
{
  const char *p = path;

  while (*p != '\0')
    {
      const char *e = p;

      while (*e != '\0' && *e != '/')
        {
          e++;
        }

      if ((e - p == 1 && p[0] == '.') || (e - p == 2 && p[0] == '.' && p[1] == '.'))
        {
          return true;
        }

      p = (*e == '/') ? e + 1 : e;
    }

  return false;
}

const char *vs_plat_fspath(const char *path, char *buf, size_t n)
{
  char cwd[VS_PATH_MAX];

  if (!has_dot_component(path))
    {
      return path;
    }

  if (getcwd(cwd, sizeof(cwd)) == NULL ||
      vs_path_normalize(cwd, path, buf, n) != 0)
    {
      return path;
    }

  return buf;
}

bool vs_plat_isatty(int fd)
{
  return isatty(fd) != 0;
}

long vs_plat_uid(void)
{
  return 0;
}

long vs_plat_euid(void)
{
  return 0;
}

int vs_plat_hostname(char *buf, size_t n)
{
  (void)buf;
  (void)n;
  return -1;                      /* NuttX has no portable hostname */
}

const char *vs_plat_ostype(void)
{
  return "nuttx";
}

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

/* Variables are exported only when asked to be, as everywhere else. This used
 * to return true because $(...) ran in a child `vaporshell -c` that inherited
 * only the environment; command substitution is in-process now (inproc.c), so
 * nothing needs every variable exported any more.
 */

int vs_plat_collate(const char *a, const char *b)
{
  return strcmp(a, b);                   /* no locales: byte order */
}

void vs_plat_locale_update(void)
{
}

bool vs_plat_export_all(void)
{
  return false;
}

bool vs_plat_external_fallback(const char *name)
{
  return is_tbx_command(name);
}

/* The name is returned as given, not as a path: posix_spawnp() resolves
 * NuttX's builtin apps by bare name. The lookup only decides "does this
 * exist?" -- in $PATH (NuttX exposes its builtin apps as /bin/<name>) or as
 * a tbx command.
 */

char *vs_plat_find_command(const char *name, const char *path_var, int *err)
{
  const char *path = path_var != NULL ? path_var : "/bin";
  struct sbuf_s cand;
  struct stat st;

  *err = ENOENT;
  if (name[0] == '\0')
    {
      return NULL;
    }

  if (strchr(name, '/') != NULL)
    {
      char fbuf[VS_PATH_MAX];
      const char *real = vs_plat_fspath(name, fbuf, sizeof(fbuf));

      return stat(real, &st) == 0 ? vs_xstrdup(real) : NULL;
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
      if (stat(cand.s, &st) == 0)
        {
          sb_free(&cand);
          return vs_xstrdup(name);
        }

      if (path[len] == '\0')
        {
          break;
        }

      path += len + 1;
    }

  sb_free(&cand);
  return is_tbx_command(name) ? vs_xstrdup(name) : NULL;
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
  /* Prefer a descriptor >= 10 so it cannot collide with one a script uses
   * (`exec 3>file`); plain dup() (the lowest free) is the fallback.
   */

  int hi = fcntl(fd, F_DUPFD, 10);

  return hi >= 0 ? hi : dup(fd);
}

char *vs_plat_capture_via_self(const char *text, int *status)
{
  int pipefd[2];
  posix_spawn_file_actions_t actions;
  pid_t pid;
  char *argv[6];
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

  /* The child is a fresh shell: pass it the language mode and the options
   * that must carry over (a real subshell would inherit them).
   */

  {
    char opts[16];
    int na = 0;
    size_t k = 1;

    opts[0] = '-';
    if (g_sh.opt_e && vs_feat(VF_ERREXIT_IN_CMDSUB)) opts[k++] = 'e';
    if (g_sh.opt_u) opts[k++] = 'u';
    if (g_sh.opt_f) opts[k++] = 'f';
    if (g_sh.opt_x) opts[k++] = 'x';
    if (g_sh.opt_C) opts[k++] = 'C';
    opts[k] = '\0';

    argv[na++] = (char *)g_sh.self;
    if (vs_mode_get() == VS_PROFILE_POSIX)
      {
        argv[na++] = "--posix";
      }

    if (k > 1)
      {
        argv[na++] = opts;
      }

    argv[na++] = "-c";
    argv[na++] = (char *)text;
    argv[na] = NULL;
  }

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
