/*
 * vars.c -- the variable table, positional parameters and function table.
 *
 * Variables live here, not in environ: only VF_EXPORT ones reach a
 * child's environment (var_build_env). That is what makes unexported
 * variables, readonly, and later arrays and local scopes possible.
 */

#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vaporshell.h"
#include "ast.h"
#include "platform.h"

struct shell_s g_sh;

bool is_valid_name(const char *s, size_t len)
{
  size_t i;

  if (len == 0 || !((s[0] >= 'A' && s[0] <= 'Z') ||
                    (s[0] >= 'a' && s[0] <= 'z') || s[0] == '_'))
    {
      return false;
    }

  for (i = 1; i < len; i++)
    {
      if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') ||
            (s[i] >= '0' && s[i] <= '9') || s[i] == '_'))
        {
          return false;
        }
    }

  return true;
}

struct var_s *var_lookup(const char *name)
{
  struct var_s *v;

  for (v = g_sh.vars; v != NULL; v = v->next)
    {
      if (strcmp(v->name, name) == 0)
        {
          return v;
        }
    }

  return NULL;
}

const char *var_get(const char *name)
{
  struct var_s *v = var_lookup(name);

  return v != NULL ? v->value : NULL;
}

static struct var_s *var_create(const char *name)
{
  struct var_s *v = vs_xmalloc(sizeof(*v));

  v->name = vs_xstrdup(name);
  v->value = NULL;
  v->flags = 0;
  v->next = g_sh.vars;
  g_sh.vars = v;
  return v;
}

int var_set(const char *name, const char *value)
{
  struct var_s *v = var_lookup(name);

  if (v == NULL)
    {
      v = var_create(name);

      /* NuttX has no in-process subshell yet, so a child shell only
       * inherits what is exported: keep the historical behaviour there
       * (every variable is an environment variable).
       */

      if (vs_plat_export_all())
        {
          v->flags |= VF_EXPORT;
        }
    }
  else if ((v->flags & VF_READONLY) != 0)
    {
      vs_err("%s: readonly variable", name);
      return -1;
    }

  free(v->value);
  v->value = vs_xstrdup(value);
  return 0;
}

int var_set_flags(const char *name, unsigned flags)
{
  struct var_s *v = var_lookup(name);

  if (v == NULL)
    {
      v = var_create(name);
    }

  v->flags |= flags;
  return 0;
}

int var_unset(const char *name)
{
  struct var_s **pv;

  for (pv = &g_sh.vars; *pv != NULL; pv = &(*pv)->next)
    {
      struct var_s *v = *pv;

      if (strcmp(v->name, name) == 0)
        {
          if ((v->flags & VF_READONLY) != 0)
            {
              vs_err("%s: readonly variable", name);
              return -1;
            }

          *pv = v->next;
          free(v->name);
          free(v->value);
          free(v);
          return 0;
        }
    }

  return 0;
}

/* Environment for a child: exported variables that have a value. */

char **var_build_env(void)
{
  struct var_s *v;
  size_t n = 0;
  size_t i = 0;
  char **env;

  for (v = g_sh.vars; v != NULL; v = v->next)
    {
      if ((v->flags & VF_EXPORT) != 0 && v->value != NULL)
        {
          n++;
        }
    }

  env = vs_xmalloc((n + 1) * sizeof(char *));
  for (v = g_sh.vars; v != NULL; v = v->next)
    {
      if ((v->flags & VF_EXPORT) != 0 && v->value != NULL)
        {
          size_t nl = strlen(v->name);
          size_t vl = strlen(v->value);
          char *e = vs_xmalloc(nl + vl + 2);

          memcpy(e, v->name, nl);
          e[nl] = '=';
          memcpy(e + nl + 1, v->value, vl + 1);
          env[i++] = e;
        }
    }

  env[i] = NULL;
  return env;
}

void env_free(char **env)
{
  size_t i;

  for (i = 0; env[i] != NULL; i++)
    {
      free(env[i]);
    }

  free(env);
}

void vars_import(char **list)
{
  size_t i;

  for (i = 0; list != NULL && list[i] != NULL; i++)
    {
      const char *eq = strchr(list[i], '=');

      if (eq != NULL && is_valid_name(list[i], (size_t)(eq - list[i])))
        {
          char *name = vs_xstrndup(list[i], (size_t)(eq - list[i]));
          struct var_s *v = var_create(name);

          v->value = vs_xstrdup(eq + 1);
          v->flags = VF_EXPORT;
          free(name);
        }
    }
}

/* ---- Positional parameters ----------------------------------------------- */

void pos_set(char **args, int n)
{
  int i;

  for (i = 0; i < g_sh.npos; i++)
    {
      free(g_sh.pos[i]);
    }

  free(g_sh.pos);
  g_sh.pos = vs_xmalloc((size_t)(n > 0 ? n : 1) * sizeof(char *));
  g_sh.npos = n;
  for (i = 0; i < n; i++)
    {
      g_sh.pos[i] = vs_xstrdup(args[i]);
    }
}

/* Installs 'args' (already owned by the caller) and returns the old list;
 * used around function calls.
 */

char **pos_swap(char **args, int n, int *old_n)
{
  char **old = g_sh.pos;

  *old_n = g_sh.npos;
  g_sh.pos = args;
  g_sh.npos = n;
  return old;
}

const char *pos_get(int i)
{
  return (i >= 1 && i <= g_sh.npos) ? g_sh.pos[i - 1] : NULL;
}

/* ---- Functions ------------------------------------------------------------- */

struct func_s *func_find(const char *name)
{
  struct func_s *f;

  for (f = g_sh.funcs; f != NULL; f = f->next)
    {
      if (strcmp(f->name, name) == 0)
        {
          return f;
        }
    }

  return NULL;
}

void func_unset(const char *name)
{
  struct func_s **pf;

  for (pf = &g_sh.funcs; *pf != NULL; pf = &(*pf)->next)
    {
      struct func_s *f = *pf;

      if (strcmp(f->name, name) == 0)
        {
          *pf = f->next;
          arena_release(f->arena);
          free(f->name);
          free(f);
          return;
        }
    }
}

void func_define(const char *name, struct node_s *body, struct arena_s *arena)
{
  struct func_s *f;

  func_unset(name);
  f = vs_xmalloc(sizeof(*f));
  f->name = vs_xstrdup(name);
  f->body = body;
  f->arena = arena;
  arena_retain(arena);
  f->next = g_sh.funcs;
  g_sh.funcs = f;
}

void shell_init(const char *arg0)
{
  char cwd[512];

  memset(&g_sh, 0, sizeof(g_sh));
  g_sh.arg0 = arg0;
  g_sh.pid = getpid();
  g_sh.cmdsub_status = -1;
  g_sh.self = "vaporshell";
  vars_import(environ);

  /* IFS is never inherited from the environment. */

  var_unset("IFS");
  var_set("IFS", " \t\n");
  var_lookup("IFS")->flags &= ~(unsigned)VF_EXPORT;

#ifdef VAPORSHELL_POSIX
  {
    char ppid[24];

    snprintf(ppid, sizeof(ppid), "%ld", (long)getppid());
    var_unset("PPID");
    var_set("PPID", ppid);
  }
#endif

  if (var_get("SHELL") == NULL)
    {
      var_set("SHELL", "vaporshell");
      var_set_flags("SHELL", VF_EXPORT);
    }

  if (getcwd(cwd, sizeof(cwd)) != NULL)
    {
      var_set("PWD", cwd);
      var_set_flags("PWD", VF_EXPORT);
    }
}
