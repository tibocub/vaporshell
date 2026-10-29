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
#include <poll.h>
#include <termios.h>
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

#define VASH_MAX_INSTANCES 16

static struct
{
  pid_t pid;                    /* 0: free */
  struct shell_s *sh;
} g_instances[VASH_MAX_INSTANCES];

struct shell_s *vash_plat_state_create(void)
{
  struct shell_s *sh = vash_xmalloc(sizeof(*sh));
  pid_t me = getpid();
  int i;

  memset(sh, 0, sizeof(*sh));
  sched_lock();
  for (i = 0; i < VASH_MAX_INSTANCES; i++)
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

struct shell_s *vash_state(void)
{
  pid_t me = getpid();
  int i;

  for (i = 0; i < VASH_MAX_INSTANCES; i++)
    {
      if (g_instances[i].pid == me)
        {
          return g_instances[i].sh;
        }
    }

  return vash_plat_state_create();       /* a task we have not seen: start fresh */
}

void vash_plat_state_destroy(void)
{
  pid_t me = getpid();
  int i;

  for (i = 0; i < VASH_MAX_INSTANCES; i++)
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

const char *vash_plat_fspath(const char *path, char *buf, size_t n)
{
  char cwd[VASH_PATH_MAX];

  if (!has_dot_component(path))
    {
      return path;
    }

  if (getcwd(cwd, sizeof(cwd)) == NULL ||
      vash_path_normalize(cwd, path, buf, n) != 0)
    {
      return path;
    }

  return buf;
}

int vash_plat_columns(void)
{
  return -1;                 /* no ioctl(TIOCGWINSZ) here; $COLUMNS is the only lever */
}

bool vash_plat_isatty(int fd)
{
  return isatty(fd) != 0;
}

int vash_plat_rows(void)
{
  return -1;
}

/* A NuttX console starts out in canonical mode with echo: the serial driver
 * itself echoes what is typed, swallows backspace and delete, and turns CR into
 * LF. The editor does all of that, so they are switched off while it runs. The
 * flags are the driver's own, kept whether or not CONFIG_SERIAL_TERMIOS is set.
 */

static struct termios g_cooked;
static bool g_is_raw;

bool vash_plat_tty_raw(bool on)
{
  struct termios t;

  if (!on)
    {
      if (g_is_raw)
        {
          tcsetattr(STDIN_FILENO, TCSADRAIN, &g_cooked);
          g_is_raw = false;
        }

      return true;
    }

  if (g_is_raw)
    {
      return true;
    }

  if (tcgetattr(STDIN_FILENO, &g_cooked) != 0)
    {
      return true;                  /* not a termios device: bytes arrive raw */
    }

  t = g_cooked;
  t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  if (tcsetattr(STDIN_FILENO, TCSADRAIN, &t) == 0)
    {
      g_is_raw = true;
    }

  return true;
}

int vash_plat_tty_read(unsigned char *c, int timeout_ms)
{
  for (; ; )
    {
      struct pollfd pfd;
      ssize_t n;

      pfd.fd = STDIN_FILENO;
      pfd.events = POLLIN;
      n = poll(&pfd, 1, timeout_ms);
      if (n < 0 && errno == EINTR)
        {
          continue;
        }

      if (n < 0)
        {
          return -1;
        }

      if (n == 0)
        {
          return 0;
        }

      n = read(STDIN_FILENO, c, 1);
      if (n < 0 && errno == EINTR)
        {
          continue;
        }

      return n == 1 ? 1 : -1;
    }
}

long vash_plat_uid(void)
{
  return 0;
}

long vash_plat_euid(void)
{
  return 0;
}

int vash_plat_hostname(char *buf, size_t n)
{
  (void)buf;
  (void)n;
  return -1;                      /* NuttX has no portable hostname */
}

const char *vash_plat_hosttype(void)
{
  return CONFIG_ARCH;
}

const char *vash_plat_ostype(void)
{
  return "nuttx";
}

bool vash_plat_interactive(void)
{
  return true;
}

bool vash_plat_have_fork(void)
{
  return false;
}

pid_t vash_plat_fork(void)
{
  errno = ENOSYS;
  return -1;
}

/* Variables are exported only when asked to be, as everywhere else. This used
 * to return true because $(...) ran in a child `vaporshell -c` that inherited
 * only the environment; command substitution is in-process now (inproc.c), so
 * nothing needs every variable exported any more.
 */

int vash_plat_collate(const char *a, const char *b)
{
  return strcmp(a, b);                   /* no locales: byte order */
}

void vash_plat_locale_update(void)
{
}

bool vash_plat_export_all(void)
{
  return false;
}

/* No locales, so a character is a byte (see mb.c). */

bool vash_plat_multibyte(void)
{
  return false;
}

size_t vash_plat_mbdecode(const char *s, size_t n, long *wc)
{
  (void)n;
  *wc = (unsigned char)s[0];
  return 1;
}

size_t vash_plat_mbencode(long wc, char *out)
{
  out[0] = (char)wc;
  return 1;
}

bool vash_plat_wc_isclass(long wc, const char *name)
{
  (void)wc;
  (void)name;
  return false;
}

long vash_plat_wc_toupper(long wc)
{
  return wc;
}

long vash_plat_wc_tolower(long wc)
{
  return wc;
}

bool vash_plat_external_fallback(const char *name)
{
  return is_tbx_command(name);
}

/* ---- #! scripts -------------------------------------------------------------
 *
 * NuttX's exec only runs binaries and builtin apps, so a script that starts
 * with "#!interpreter [arg]" is started by running the interpreter on it,
 * the way Linux does: argv becomes { interpreter, [arg], script, args... }.
 * "#!/usr/bin/env prog [more]" runs prog (found by name: builtins have no
 * path worth trusting) with the rest as separate words; env's own options
 * (-S, VAR=value) are not supported.
 */

#define VASH_SHEBANG_MAX 128

static bool read_shebang(const char *path, char *line, size_t size)
{
  int fd = open(path, O_RDONLY);
  ssize_t n;

  if (fd < 0)
    {
      return false;
    }

  n = read(fd, line, size - 1);
  close(fd);
  if (n < 2 || line[0] != '#' || line[1] != '!')
    {
      return false;
    }

  line[n] = '\0';
  line[strcspn(line, "\r\n")] = '\0';
  memmove(line, line + 2, strlen(line + 2) + 1);
  return true;
}

/* Fill out[] (NULL terminated, at most nout entries) from the #! text and
 * the original command; returns the interpreter to spawn, or NULL.
 */

static const char *shebang_argv(char *line, const char *path,
                                char *const argv[], char **out, int nout)
{
  char *interp = line + strspn(line, " \t");
  char *rest = interp + strcspn(interp, " \t");
  const char *base;
  int n = 0;
  int i;

  if (*interp == '\0')
    {
      return NULL;
    }

  if (*rest != '\0')
    {
      *rest++ = '\0';
      rest += strspn(rest, " \t");
    }

  base = strrchr(interp, '/');
  base = base != NULL ? base + 1 : interp;
  if (strcmp(base, "env") == 0 && *rest != '\0')
    {
      interp = rest;
      rest = interp + strcspn(interp, " \t");
      if (*rest != '\0')
        {
          *rest++ = '\0';
          rest += strspn(rest, " \t");
        }
    }

  out[n++] = interp;
  while (*rest != '\0' && n < nout - 2)
    {
      out[n++] = rest;
      rest += strcspn(rest, " \t");
      if (*rest != '\0')
        {
          *rest++ = '\0';
          rest += strspn(rest, " \t");
        }
    }

  out[n++] = (char *)path;
  for (i = 1; argv[i] != NULL && n < nout - 1; i++)
    {
      out[n++] = argv[i];
    }

  out[n] = NULL;
  return interp;
}

/* The name is returned as given, not as a path: posix_spawnp() resolves
 * NuttX's builtin apps by bare name. The lookup only decides "does this
 * exist?" -- in $PATH (NuttX exposes its builtin apps as /bin/<name>) or as
 * a tbx command.
 */

char *vash_plat_find_command(const char *name, const char *path_var, int *err)
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
      char fbuf[VASH_PATH_MAX];
      const char *real = vash_plat_fspath(name, fbuf, sizeof(fbuf));

      return stat(real, &st) == 0 ? vash_xstrdup(real) : NULL;
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
          char head[VASH_SHEBANG_MAX];
          char *found = vash_xstrdup(S_ISREG(st.st_mode) &&
                                   read_shebang(cand.s, head, sizeof(head))
                                   ? cand.s : name);

          sb_free(&cand);
          return found;
        }

      if (path[len] == '\0')
        {
          break;
        }

      path += len + 1;
    }

  sb_free(&cand);
  return is_tbx_command(name) ? vash_xstrdup(name) : NULL;
}

int vash_plat_spawn(const char *path, char *const argv[], char *const envp[],
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

  {
    char line[VASH_SHEBANG_MAX];
    char *sb[40];
    const char *interp;

    if (strchr(path, '/') != NULL && read_shebang(path, line, sizeof(line)) &&
        (interp = shebang_argv(line, path, argv, sb, 40)) != NULL)
      {
        ret = posix_spawnp(pid, interp, &actions, NULL, sb, envp);
        if (ret == ENOENT && strchr(interp, '/') != NULL)
          {
            /* "#!/bin/lua": NuttX builtins are found by bare name */

            sb[0] = strrchr(interp, '/') + 1;
            ret = posix_spawnp(pid, sb[0], &actions, NULL, sb, envp);
          }

        posix_spawn_file_actions_destroy(&actions);
        return ret;
      }
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

int vash_plat_exec(const char *path, char *const argv[], char *const envp[])
{
  (void)path;
  (void)argv;
  (void)envp;
  errno = ENOSYS;
  return -1;
}

int vash_plat_dup_high(int fd)
{
  /* Prefer a descriptor >= 10 so it cannot collide with one a script uses
   * (`exec 3>file`); plain dup() (the lowest free) is the fallback.
   */

  int hi = fcntl(fd, F_DUPFD, 10);

  return hi >= 0 ? hi : dup(fd);
}

char *vash_plat_capture_via_self(const char *text, int *status)
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
    if (g_sh.opt_e && vash_feat(VF_ERREXIT_IN_CMDSUB)) opts[k++] = 'e';
    if (g_sh.opt_u) opts[k++] = 'u';
    if (g_sh.opt_f) opts[k++] = 'f';
    if (g_sh.opt_x) opts[k++] = 'x';
    if (g_sh.opt_C) opts[k++] = 'C';
    opts[k] = '\0';

    argv[na++] = (char *)g_sh.self;
    if (vash_mode_get() == VASH_PROFILE_POSIX)
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
