/*
 * vars.c -- the variable table, positional parameters and function table.
 *
 * Variables live here, not in environ: only VF_EXPORT ones reach a
 * child's environment (var_build_env). That is what makes unexported
 * variables, readonly, and later arrays and local scopes possible.
 */

#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vaporshell.h"
#include "expand.h"
#include "ast.h"
#include "platform.h"
#include "mode.h"
#include "exec.h"

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

  if (g_sh.lazy_dirty != 0 && (name[0] == 'P' || name[0] == 'F' || name[0] == 'B'))
    {
      special_refresh(name);            /* PIPESTATUS, FUNCNAME, BASH_*: built on demand */
    }

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

  if (v != NULL && v->arr != NULL)
    {
      return v->arr->assoc ? arr_get_key(v->arr, "0")
                           : arr_get(v->arr, 0);        /* $a is ${a[0]} */
    }

  return v != NULL ? v->value : NULL;
}

static struct var_s *var_create(const char *name)
{
  struct var_s *v = vs_xmalloc(sizeof(*v));

  v->name = vs_xstrdup(name);
  v->value = NULL;
  v->flags = 0;
  v->arr = NULL;
  v->next = g_sh.vars;
  g_sh.vars = v;
  return v;
}

/* The variables that choose the collation locale (see vs_plat_locale_update). */

bool var_is_locale_var(const char *n)
{
  return n[0] == 'L' && (strcmp(n, "LC_ALL") == 0 || strcmp(n, "LC_COLLATE") == 0 ||
                         strcmp(n, "LANG") == 0);
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

      /* a platform may want every variable exported (none does today) */

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

  v->flags &= ~(unsigned)VF_NOVALUE;
  if ((v->flags & (VF_INTEGER | VF_LOWER | VF_UPPER)) != 0)
    {
      char *cv = var_attr_value(v->flags, value);
      int r = 0;

      if (v->arr != NULL)
        {
          if (v->arr->assoc)
            {
              arr_set_key(v->arr, "0", cv);
            }
          else
            {
              arr_set(v->arr, 0, cv);
            }
        }
      else
        {
          free(v->value);
          v->value = vs_xstrdup(cv);
        }

      free(cv);
      return r;
    }

  if (v->arr != NULL)
    {
      if (v->arr->assoc)
        {
          arr_set_key(v->arr, "0", value);    /* a=x on an array sets a[0] */
        }
      else
        {
          arr_set(v->arr, 0, value);
        }

      return 0;
    }

  free(v->value);
  v->value = vs_xstrdup(value);
  if (name[0] == 'P' && strcmp(name, "PATH") == 0)
    {
      hash_clear();                   /* remembered locations may be stale */
    }

  if (var_is_locale_var(name))
    {
      vs_plat_locale_update();
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
          arr_free(v->arr);
          free(v);
          if (var_is_locale_var(name))
            {
              vs_plat_locale_update();
            }

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
          free(f->src);
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
  f->src = vs_xstrdup(g_sh.cur_src != NULL ? g_sh.cur_src : "");
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
  vs_shopt_defaults();
  special_init();
  g_sh.arg0 = arg0;
  g_sh.cur_src = arg0;
  g_sh.pid = getpid();
  g_sh.cmdsub_status = -1;
  g_sh.self = "vaporshell";
  g_sh.force_inproc = getenv("VS_INPROC") != NULL;    /* test hook, see inproc.c */
  vars_import(environ);
  vs_plat_locale_update();              /* LC_ALL etc. from the environment */

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
      arr_free(v->arr);
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
      free(g_sh.trap_parent[i]);
    }

  var_locals_pop(0);
  dirstack_free();
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
      l->old_arr = NULL;
      if (v != NULL && v->arr != NULL && !inherit)
        {
          l->old_arr = v->arr;           /* the outer array waits here until the function returns */
          v->arr = NULL;
        }
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
      if (l->had || l->old_arr != NULL)
        {
          if (v == NULL)
            {
              v = var_create(l->name);
            }

          free(v->value);
          v->value = l->old;
          arr_free(v->arr);
          v->arr = l->old_arr;
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


/* ---- Arrays: the variable-level calls ---------------------------------------- */

/* The array behind 'name'. With create, a scalar is turned into an array whose
 * element 0 is its old value (bash does the same for `a[1]=x` on `a=v`), and
 * a missing variable is created empty.
 */

struct arr_s *var_array(const char *name, bool create)
{
  struct var_s *v = var_lookup(name);

  if (v != NULL && v->arr != NULL)
    {
      return v->arr;
    }

  if (!create)
    {
      return NULL;
    }

  if (v == NULL)
    {
      v = var_create(name);
    }
  else if ((v->flags & VF_READONLY) != 0)
    {
      vs_err("%s: readonly variable", name);
      return NULL;
    }

  v->arr = arr_new();
  if (v->value != NULL)
    {
      arr_set(v->arr, 0, v->value);
      free(v->value);
      v->value = NULL;
    }

  return v->arr;
}

bool var_is_array(const char *name)
{
  struct var_s *v = var_lookup(name);

  return v != NULL && v->arr != NULL;
}

const char *var_elem_get(const char *name, long idx)
{
  struct var_s *v = var_lookup(name);

  if (v == NULL)
    {
      return idx == 0 ? var_get(name) : NULL;   /* computed variables have an element 0 */
    }

  if (v->arr != NULL)
    {
      if (v->arr->assoc)
        {
          char k[24];

          snprintf(k, sizeof(k), "%ld", idx);
          return arr_get_key(v->arr, k);
        }

      return arr_get(v->arr, idx);
    }

  return idx == 0 ? v->value : NULL;
}

int var_elem_set(const char *name, long idx, const char *val)
{
  struct var_s *v = var_lookup(name);
  struct arr_s *a;

  if (v != NULL && (v->flags & VF_READONLY) != 0)
    {
      vs_err("%s: readonly variable", name);
      return -1;
    }

  a = var_array(name, true);
  if (a == NULL)
    {
      return -1;
    }

  v = var_lookup(name);
  v->flags &= ~(unsigned)VF_NOVALUE;
  {
    char *cv = (v->flags & (VF_INTEGER | VF_LOWER | VF_UPPER)) != 0
               ? var_attr_value(v->flags, val) : vs_xstrdup(val);

    if (a->assoc)
      {
        char k[24];

        snprintf(k, sizeof(k), "%ld", idx);
        arr_set_key(a, k, cv);
      }
    else
      {
        arr_set(a, idx, cv);
      }

    free(cv);
  }

  if (g_sh.opt_a)
    {
      var_lookup(name)->flags |= VF_EXPORT;
    }

  return 0;
}

int var_elem_unset(const char *name, long idx)
{
  struct var_s *v = var_lookup(name);

  if (v == NULL)
    {
      return 0;
    }

  if ((v->flags & VF_READONLY) != 0)
    {
      vs_err("%s: readonly variable", name);
      return -1;
    }

  if (v->arr != NULL)
    {
      arr_unset(v->arr, idx);
    }
  else if (idx == 0)
    {
      return var_unset(name);          /* unset a[0] on a scalar unsets it */
    }

  return 0;
}

/* name=(...): the finished array replaces whatever the variable was. */

int var_array_replace(const char *name, struct arr_s *arr)
{
  struct var_s *v = var_lookup(name);

  if (v == NULL)
    {
      v = var_create(name);
    }
  else if ((v->flags & VF_READONLY) != 0)
    {
      vs_err("%s: readonly variable", name);
      arr_free(arr);                       /* the array is ours to take, even if refused */
      return -1;
    }

  free(v->value);
  v->value = NULL;
  arr_free(v->arr);
  v->arr = arr;
  v->flags &= ~(unsigned)VF_NOVALUE;
  if (g_sh.opt_a)
    {
      v->flags |= VF_EXPORT;
    }

  return 0;
}


/* ---- Attributes and associative arrays ------------------------------------------- */

char *var_attr_value(unsigned flags, const char *value)
{
  char *r;
  size_t i;

  if ((flags & VF_INTEGER) != 0)
    {
      long n = 0;
      char buf[32];

      if (value[0] != '\0' && arith_eval(value, &n) != 0)
        {
          n = 0;                       /* the error was printed; the value is 0 */
        }

      snprintf(buf, sizeof(buf), "%ld", n);
      r = vs_xstrdup(buf);
    }
  else
    {
      r = vs_xstrdup(value);
    }

  for (i = 0; r[i] != '\0'; i++)
    {
      if ((flags & VF_LOWER) != 0)
        {
          r[i] = (char)tolower((unsigned char)r[i]);
        }
      else if ((flags & VF_UPPER) != 0)
        {
          r[i] = (char)toupper((unsigned char)r[i]);
        }
    }

  return r;
}

bool var_is_assoc(const char *name)
{
  struct var_s *v = var_lookup(name);

  return v != NULL && v->arr != NULL && v->arr->assoc;
}

/* The associative array behind 'name'; with create, a missing variable
 * becomes one. An indexed array cannot be converted, as in bash.
 */

struct arr_s *var_assoc(const char *name, bool create)
{
  struct var_s *v = var_lookup(name);

  if (v != NULL && v->arr != NULL)
    {
      if (v->arr->assoc)
        {
          return v->arr;
        }

      if (create)
        {
          vs_err("%s: cannot convert indexed to associative array", name);
        }

      return NULL;
    }

  if (!create)
    {
      return NULL;
    }

  if (v == NULL)
    {
      v = var_create(name);
    }
  else if ((v->flags & VF_READONLY) != 0)
    {
      vs_err("%s: readonly variable", name);
      return NULL;
    }

  v->arr = arr_new_assoc();
  if (v->value != NULL)
    {
      arr_set_key(v->arr, "0", v->value);      /* the scalar becomes element "0" */
      free(v->value);
      v->value = NULL;
    }

  return v->arr;
}

const char *var_ref_get(const char *name, const struct subref_s *r)
{
  if (r->key != NULL)
    {
      struct var_s *v = var_lookup(name);

      return (v != NULL && v->arr != NULL && v->arr->assoc)
             ? arr_get_key(v->arr, r->key) : NULL;
    }

  return var_elem_get(name, r->idx);
}

int var_ref_set(const char *name, const struct subref_s *r, const char *val)
{
  struct var_s *v;
  struct arr_s *a;
  char *cv;

  if (r->key == NULL)
    {
      return var_elem_set(name, r->idx, val);
    }

  v = var_lookup(name);
  if (v != NULL && (v->flags & VF_READONLY) != 0)
    {
      vs_err("%s: readonly variable", name);
      return -1;
    }

  a = var_assoc(name, true);
  if (a == NULL)
    {
      return -1;
    }

  v = var_lookup(name);
  v->flags &= ~(unsigned)VF_NOVALUE;
  cv = (v->flags & (VF_INTEGER | VF_LOWER | VF_UPPER)) != 0
       ? var_attr_value(v->flags, val) : vs_xstrdup(val);
  arr_set_key(a, r->key, cv);
  free(cv);
  return 0;
}

int var_ref_unset(const char *name, const struct subref_s *r)
{
  struct var_s *v;

  if (r->key == NULL)
    {
      return var_elem_unset(name, r->idx);
    }

  v = var_lookup(name);
  if (v == NULL || v->arr == NULL || !v->arr->assoc)
    {
      return 0;
    }

  if ((v->flags & VF_READONLY) != 0)
    {
      vs_err("%s: readonly variable", name);
      return -1;
    }

  arr_unset_key(v->arr, r->key);
  return 0;
}

void subref_free(struct subref_s *r)
{
  free(r->key);
  r->key = NULL;
}
