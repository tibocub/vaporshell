/*
 * rcfile.c -- which startup files an interactive shell reads, and in what
 * order. See rcfile.h and docs/interactive.md.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "vaporshell.h"
#include "exec.h"
#include "platform.h"
#include "rcfile.h"

#define SYSTEM_RC "/etc/vaporshell/vash.conf"

static bool readable(const char *path)
{
  struct stat st;

  return stat(VASH_FS(path), &st) == 0 && S_ISREG(st.st_mode);
}

/* Run a file as the shell's own startup code: its errors are reported but do
 * not end the session, and its status is not the first prompt's $?.
 */

bool vash_rc_source(const char *path)
{
  if (!readable(path))
    {
      return false;
    }

  run_file(path);
  g_sh.unwind = UW_NONE;
  return true;
}

static char *in_home(const char *name)
{
  const char *home = var_get("HOME");
  char *path;

  if (home == NULL || *home == '\0')
    {
#ifdef VAPORSHELL_POSIX
      return NULL;
#else
      home = "/data";
#endif
    }

  path = vash_xmalloc(strlen(home) + strlen(name) + 2);
  sprintf(path, "%s/%s", home, name);
  return path;
}

static void source_home(const char *name)
{
  char *path = in_home(name);

  if (path != NULL)
    {
      vash_rc_source(path);
      free(path);
    }
}

static void load(bool login, bool norc, const char *rcfile)
{
  char *path;
  const char *lbf;
  size_t i;

  if (norc)
    {
      return;
    }

  vash_rc_source(SYSTEM_RC);
  path = var_get("INPUTRC") != NULL ? vash_xstrdup(var_get("INPUTRC")) : in_home(".inputrc");
  if (path != NULL)
    {
      if (readable(path))
        {
          char *argv[4] = { "bind", "-f", path, NULL };

          bi_bind(3, argv);
        }

      free(path);
    }

  if (rcfile == NULL && var_get("VASH_RC") != NULL && *var_get("VASH_RC") != '\0')
    {
      rcfile = var_get("VASH_RC");
    }

  if (rcfile != NULL)
    {
      vash_rc_source(rcfile);
      return;
    }

  source_home(".vashrc");            /* first: it may set LOAD_BASH_FILES=0 */

  lbf = var_get("LOAD_BASH_FILES");
  if (lbf != NULL && strcmp(lbf, "0") == 0)
    {
      return;
    }

  if (!login)
    {
      source_home(".bashrc");
      return;
    }

  /* bash's login order: /etc/profile, then the first of these that exists */

  vash_rc_source("/etc/profile");
  for (i = 0; i < 3; i++)
    {
      static const char *const names[] = { ".bash_profile", ".bash_login", ".profile" };

      path = in_home(names[i]);
      if (path != NULL)
        {
          bool found = vash_rc_source(path);

          free(path);
          if (found)
            {
              break;
            }
        }
    }
}

void vash_rc_load(bool login, bool norc, const char *rcfile)
{
  load(login, norc, rcfile);
  g_sh.last_status = 0;                /* the first prompt starts clean whatever the files ended with */
}
