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
#include <sys/utsname.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "vaporshell.h"
#include "platform.h"

struct shell_s *vs_plat_state_create(void)
{
  memset(&g_vs_state, 0, sizeof(g_vs_state));
  return &g_vs_state;
}

void vs_plat_state_destroy(void)
{
  memset(&g_vs_state, 0, sizeof(g_vs_state));
}

const char *vs_plat_fspath(const char *path, char *buf, size_t n)
{
  (void)buf;
  (void)n;
  return path;
}

bool vs_plat_isatty(int fd)
{
  return isatty(fd) != 0;
}

long vs_plat_uid(void)
{
  return (long)getuid();
}

long vs_plat_euid(void)
{
  return (long)geteuid();
}

int vs_plat_hostname(char *buf, size_t n)
{
  return gethostname(buf, n);
}

const char *vs_plat_ostype(void)
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

int vs_plat_collate(const char *a, const char *b)
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

void vs_plat_locale_update(void)
{
  setlocale(LC_COLLATE, locale_choice("LC_COLLATE"));
  setlocale(LC_CTYPE, locale_choice("LC_CTYPE"));       /* what a character is */
}

/* ---- Characters ------------------------------------------------------------- */

bool vs_plat_multibyte(void)
{
  return MB_CUR_MAX > 1;
}

size_t vs_plat_mbdecode(const char *s, size_t n, long *wc)
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

size_t vs_plat_mbencode(long wc, char *out)
{
  mbstate_t st;
  size_t r;

  memset(&st, 0, sizeof(st));
  r = wcrtomb(out, (wchar_t)wc, &st);
  return r == (size_t)-1 ? 0 : r;
}

bool vs_plat_wc_isclass(long wc, const char *name)
{
  wctype_t t = wctype(name);

  return t != 0 && iswctype((wint_t)wc, t) != 0;
}

long vs_plat_wc_toupper(long wc)
{
  return (long)towupper((wint_t)wc);
}

long vs_plat_wc_tolower(long wc)
{
  return (long)towlower((wint_t)wc);
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
