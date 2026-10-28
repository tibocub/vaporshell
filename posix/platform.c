/*
 * posix/platform.c -- the platform layer for the standalone build
 * (Linux, macOS, BSD): real fork(), PATH search, posix_spawn.
 */

#include <nuttx/config.h>
#include <errno.h>
#include <locale.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <poll.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "vaporshell.h"
#include "platform.h"

struct shell_s *vash_plat_state_create(void)
{
  memset(&g_vash_state, 0, sizeof(g_vash_state));
  return &g_vash_state;
}

void vash_plat_state_destroy(void)
{
  memset(&g_vash_state, 0, sizeof(g_vash_state));
}

const char *vash_plat_fspath(const char *path, char *buf, size_t n)
{
  (void)buf;
  (void)n;
  return path;
}

bool vash_plat_isatty(int fd)
{
  return isatty(fd) != 0;
}

int vash_plat_columns(void)
{
  struct winsize ws;

  if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
    {
      return ws.ws_col;
    }

  return -1;
}

int vash_plat_rows(void)
{
  struct winsize ws;

  if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
    {
      return ws.ws_row;
    }

  return -1;
}

static struct termios g_cooked;
static bool g_is_raw;
static const int g_fatal[] = { SIGTERM, SIGQUIT };
static struct sigaction g_saved_sa[2];

/* A signal that would end the shell while it edits must not leave the terminal
 * without echo: put the settings back, then die of it as it would have.
 */

static void raw_fatal(int sig)
{
  tcsetattr(STDIN_FILENO, TCSANOW, &g_cooked);
  signal(sig, SIG_DFL);
  raise(sig);
}

static void fatal_handlers(bool install)
{
  struct sigaction sa;
  size_t i;

  for (i = 0; i < sizeof(g_fatal) / sizeof(g_fatal[0]); i++)
    {
      if (!install)
        {
          if (g_saved_sa[i].sa_handler == SIG_DFL)
            {
              sigaction(g_fatal[i], &g_saved_sa[i], NULL);
            }

          continue;
        }

      sigaction(g_fatal[i], NULL, &g_saved_sa[i]);
      if (g_saved_sa[i].sa_handler == SIG_DFL)          /* a `trap` of the user's stays theirs */
        {
          memset(&sa, 0, sizeof(sa));
          sa.sa_handler = raw_fatal;
          sigaction(g_fatal[i], &sa, NULL);
        }
    }
}

bool vash_plat_tty_raw(bool on)
{
  struct termios t;

  if (!on)
    {
      if (g_is_raw)
        {
          tcsetattr(STDIN_FILENO, TCSADRAIN, &g_cooked);
          fatal_handlers(false);
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
      return false;
    }

  t = g_cooked;
  t.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  t.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
  t.c_cflag |= CS8;
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSADRAIN, &t) != 0)
    {
      return false;
    }

  fatal_handlers(true);
  g_is_raw = true;
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
  return (long)getuid();
}

long vash_plat_euid(void)
{
  return (long)geteuid();
}

int vash_plat_hostname(char *buf, size_t n)
{
  return gethostname(buf, n);
}

const char *vash_plat_ostype(void)
{
  static char os[64];
  struct utsname u;
  size_t i;

  if (uname(&u) != 0)
    {
      return "unknown";
    }

  for (i = 0; u.sysname[i] != '\0' && i < sizeof(os) - 8; i++)
    {
      char c = u.sysname[i];

      os[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }

  os[i] = '\0';
  if (strcmp(os, "linux") == 0)
    {
      strcat(os, "-gnu");
    }

  return os;
}

bool vash_plat_interactive(void)
{
  return isatty(STDIN_FILENO) != 0;
}

bool vash_plat_have_fork(void)
{
  return true;
}

pid_t vash_plat_fork(void)
{
  fflush(NULL);                 /* or buffered output would appear twice */
  return fork();
}

int vash_plat_collate(const char *a, const char *b)
{
  int r = strcoll(a, b);

  return r != 0 ? r : strcmp(a, b);      /* a total order even if the locale ties */
}

/* The C library's own precedence: LC_ALL, then the category's variable, then
 * LANG. A locale that is not installed leaves the category as it was (bash
 * does the same); with nothing set it is the C locale.
 */

static const char *locale_choice(const char *category_var)
{
  const char *v = var_get("LC_ALL");

  if (v == NULL || v[0] == '\0')
    {
      v = var_get(category_var);
    }

  if (v == NULL || v[0] == '\0')
    {
      v = var_get("LANG");
    }

  return (v != NULL && v[0] != '\0') ? v : "C";
}

void vash_plat_locale_update(void)
{
  setlocale(LC_COLLATE, locale_choice("LC_COLLATE"));
  setlocale(LC_CTYPE, locale_choice("LC_CTYPE"));       /* what a character is */
  setlocale(LC_TIME, locale_choice("LC_TIME"));         /* strftime: printf %(...)T, date-ish formats */
}

/* ---- Characters ------------------------------------------------------------- */

bool vash_plat_multibyte(void)
{
  return MB_CUR_MAX > 1;
}

size_t vash_plat_mbdecode(const char *s, size_t n, long *wc)
{
  mbstate_t st;
  wchar_t w = 0;
  size_t r;

  memset(&st, 0, sizeof(st));
  r = mbrtowc(&w, s, n, &st);
  if (r == (size_t)-2)
    {
      return 0;
    }

  if (r == (size_t)-1)
    {
      return (size_t)-1;
    }

  *wc = (long)w;
  return r == 0 ? 1 : r;                /* a NUL character is one byte */
}

size_t vash_plat_mbencode(long wc, char *out)
{
  mbstate_t st;
  size_t r;

  memset(&st, 0, sizeof(st));
  r = wcrtomb(out, (wchar_t)wc, &st);
  return r == (size_t)-1 ? 0 : r;
}

bool vash_plat_wc_isclass(long wc, const char *name)
{
  wctype_t t = wctype(name);

  return t != 0 && iswctype((wint_t)wc, t) != 0;
}

long vash_plat_wc_toupper(long wc)
{
  return (long)towupper((wint_t)wc);
}

long vash_plat_wc_tolower(long wc)
{
  return (long)towlower((wint_t)wc);
}

bool vash_plat_export_all(void)
{
  return false;
}

bool vash_plat_external_fallback(const char *name)
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

char *vash_plat_find_command(const char *name, const char *path_var, int *err)
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
      return is_executable_file(name, err) ? vash_xstrdup(name) : NULL;
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

  ret = posix_spawn(pid, path, &actions, NULL, argv, envp);
  posix_spawn_file_actions_destroy(&actions);
  return ret;
}

int vash_plat_exec(const char *path, char *const argv[], char *const envp[])
{
  return execve(path, argv, envp);
}

int vash_plat_dup_high(int fd)
{
  return fcntl(fd, F_DUPFD_CLOEXEC, 10);
}

char *vash_plat_capture_via_self(const char *text, int *status)
{
  (void)text;
  *status = 1;
  return NULL;                  /* never used: this platform forks */
}
