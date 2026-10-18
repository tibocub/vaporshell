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

#define SYSTEM_RC "/etc/vaporshell/vsh.conf"

static bool readable(const char *path)
{
  struct stat st;

  return stat(VS_FS(path), &st) == 0 && S_ISREG(st.st_mode);
}

/* Run a file as the shell's own startup code: its errors are reported but do
 * not end the session, and its status is not the first prompt's $?.
 */

bool vs_rc_source(const char *path)
{
  if (!readable(path))
    {
      return false;
    }

  run_file(path);
  g_sh.unwind = UW_NONE;
  g_sh.last_status = 0;
  return true;
}

#define source(path) vs_rc_source(path)

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

  path = vs_xmalloc(strlen(home) + strlen(name) + 2);
  sprintf(path, "%s/%s", home, name);
  return path;
}

void vs_rc_load(bool login, bool norc, const char *rcfile)
{
  char *path;

  if (login)
    {
      path = in_home(".vsh_profile");
      if (path != NULL)
        {
          source(path);
          free(path);
        }
    }

  if (norc)
    {
      return;
    }

  if (rcfile != NULL)
    {
      source(rcfile);
      return;
    }

  source(SYSTEM_RC);
  if (var_get("VS_RC") != NULL && *var_get("VS_RC") != '\0')
    {
      source(var_get("VS_RC"));
      return;
    }

  path = in_home(".vaporshellrc");
  if (path != NULL)
    {
      source(path);
      free(path);
    }
}
