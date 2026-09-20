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
#include "mode.h"

#ifdef VAPORSHELL_POSIX
struct shell_s g_vs_state;
#endif

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

/* Variables bash computes rather than stores. A stored variable of the same
 * name wins, so a script can still shadow them.
 */

static const char *bash_var(const char *name)
{
  static char buf[64];
  static char host[256];

  if (strcmp(name, "RANDOM") == 0)
    {
      unsigned x = g_sh.rand_state != 0 ? g_sh.rand_state
                                        : (unsigned)time(NULL) ^ (unsigned)g_sh.pid;

      x ^= x << 13;                    /* xorshift32: 0..32767 like bash */
      x ^= x >> 17;
      x ^= x << 5;
      g_sh.rand_state = x;
      snprintf(buf, sizeof(buf), "%u", (x >> 8) & 0x7fff);
      return buf;
    }

  if (strcmp(name, "SECONDS") == 0)
    {
      snprintf(buf, sizeof(buf), "%ld", (long)(time(NULL) - g_sh.seconds_base));
      return buf;
    }

  if (strcmp(name, "BASH_VERSION") == 0)
    {
      return "5.3.0(1)-vaporshell";
    }

  if (strcmp(name, "UID") == 0 || strcmp(name, "EUID") == 0)
    {
      snprintf(buf, sizeof(buf), "%ld",
               (long)(name[0] == 'E' ? vs_plat_euid() : vs_plat_uid()));
      return buf;
    }

  if (strcmp(name, "HOSTNAME") == 0)
    {
      return vs_plat_hostname(host, sizeof(host)) == 0 ? host : NULL;
    }

  if (strcmp(name, "OSTYPE") == 0)
    {
      return vs_plat_ostype();
    }

  return NULL;
}

const char *var_get(const char *name)
{
  struct var_s *v;

  /* $LINENO is computed, not stored: assigning it just shadows it. */

  if (name[0] == 'L' && strcmp(name, "LINENO") == 0 && g_sh.lineno > 0 &&
      var_lookup(name) == NULL)
    {
      static char buf[16];             /* per call site; read immediately */

      snprintf(buf, sizeof(buf), "%d", g_sh.lineno);
      return buf;
    }

  v = var_lookup(name);
  if (v == NULL && vs_feat(VF_BASH_VARS))
    {
      return bash_var(name);
    }

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
  struct var_s *v;

  if (vs_feat(VF_BASH_VARS) && (name[0] == 'R' || name[0] == 'S'))
    {
      /* Assigning RANDOM seeds it; assigning SECONDS restarts the count. */

      if (strcmp(name, "RANDOM") == 0)
        {
          g_sh.rand_state = (unsigned)atol(value) * 2654435761u + 1u;
          return 0;
        }

      if (strcmp(name, "SECONDS") == 0)
        {
          g_sh.seconds_base = time(NULL) - (time_t)atol(value);
          return 0;
        }
    }

  v = var_lookup(name);

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

  if (g_sh.opt_a)
    {
      v->flags |= VF_EXPORT;
    }

  free(v->value);
  v->value = vs_xstrdup(value);
  if (name[0] == 'P' && strcmp(name, "PATH") == 0)
    {
      hash_clear();                   /* remembered locations may be stale */
    }

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

  vs_plat_state_create();
  vs_mode_set(VS_PROFILE_BASH);
  g_sh.arg0 = arg0;
  g_sh.pid = getpid();
  g_sh.cmdsub_status = -1;
  g_sh.self = "vaporshell";
  vars_import(environ);

  /* getopts starts scanning at the first argument. */

  var_set("OPTIND", "1");
  strcpy(g_sh.getopts_last, "1");

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

void shell_fini(void)
{
  int i;

  while (g_sh.vars != NULL)
    {
      struct var_s *v = g_sh.vars;

      g_sh.vars = v->next;
      free(v->name);
      free(v->value);
      free(v);
    }

  while (g_sh.funcs != NULL)
    {
      func_unset(g_sh.funcs->name);
    }

  for (i = 0; i < g_sh.npos; i++)
    {
      free(g_sh.pos[i]);
    }

  free(g_sh.pos);
  for (i = 0; i < VS_NTRAPS; i++)
    {
      free(g_sh.trap_action[i]);
    }

  var_locals_pop(0);
  aliases_free();
  hash_clear();
  vs_plat_state_destroy();
}

/* ---- local ---------------------------------------------------------------- */

/* Declares 'name' local to the running function. With a value it is
 * assigned; without one bash makes it unset and dash leaves the outer
 * value visible (VF_LOCAL_INHERITS -> 'inherit').
 */

int var_local_declare(const char *name, const char *value, bool inherit)
{
  struct var_s *v = var_lookup(name);
  struct local_s *l;

  for (l = g_sh.locals; l != NULL; l = l->next)
    {
      if (l->depth == g_sh.func_depth && strcmp(l->name, name) == 0)
        {
          break;                       /* already local here: keep the first save */
        }
    }

  if (l == NULL)
    {
      l = vs_xmalloc(sizeof(*l));
      l->name = vs_xstrdup(name);
      l->had = v != NULL && v->value != NULL;
      l->old = l->had ? vs_xstrdup(v->value) : NULL;
      l->flags = v != NULL ? v->flags : 0;
      l->depth = g_sh.func_depth;
      l->next = g_sh.locals;
      g_sh.locals = l;
    }

  if (value != NULL)
    {
      return var_set(name, value);
    }

  if (!inherit && v != NULL && v->value != NULL)
    {
      free(v->value);
      v->value = NULL;
    }

  return 0;
}

void var_locals_pop(int depth)
{
  while (g_sh.locals != NULL && g_sh.locals->depth >= depth)
    {
      struct local_s *l = g_sh.locals;
      struct var_s *v = var_lookup(l->name);

      g_sh.locals = l->next;
      if (l->had)
        {
          if (v == NULL)
            {
              v = var_create(l->name);
            }

          free(v->value);
          v->value = l->old;
          v->flags = l->flags;
        }
      else
        {
          free(l->old);
          if (v != NULL)
            {
              v->flags &= ~(unsigned)VF_READONLY;
              var_unset(l->name);
            }
        }

      free(l->name);
      free(l);
    }
}
