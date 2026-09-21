/*
 * declare.c -- the declaration builtins in bash mode: declare, typeset,
 * local, export and readonly.
 *
 * They share one implementation because they share one job: create or find a
 * variable, give it attributes (-a -A -i -l -u -r -x), optionally assign it,
 * and print variables back as `declare` lines (-p). Their arguments are not
 * ordinary words: exec.c expands an assignment-shaped argument without word
 * splitting, and hands an array literal over unexpanded (g_sh.decl_raw), so
 * `declare -a a=("$x" y)` keeps its quoting.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "mode.h"
#include "expand.h"
#include "exec.h"

enum decl_kind_e
{
  DK_DECLARE,
  DK_LOCAL,
  DK_EXPORT,
  DK_READONLY
};

struct dopts_s
{
  bool a;              /* -a */
  bool A;              /* -A */
  bool i;              /* -i */
  bool l;              /* -l */
  bool u;              /* -u */
  bool r;              /* -r */
  bool x;              /* -x */
  bool p;              /* -p */
  bool g;              /* -g */
  bool f;              /* -f */
  bool F;              /* -F */
  bool ni;             /* +i */
  bool nl;             /* +l */
  bool nu;             /* +u */
  bool nx;             /* +x, or export -n */
  bool any;            /* some attribute option was given */
};

static const char *kind_word(enum decl_kind_e k)
{
  switch (k)
    {
      case DK_LOCAL:    return "local";
      case DK_EXPORT:   return "export";
      case DK_READONLY: return "readonly";
      default:          return "declare";
    }
}

/* ---- Printing -------------------------------------------------------------------- */

static void flag_letters(const struct var_s *v, char *out)
{
  char *t = out;

  /* bash's fixed order: a A i r x l u */

  if (v->arr != NULL)
    {
      *t++ = v->arr->assoc ? 'A' : 'a';
    }

  if ((v->flags & VF_INTEGER) != 0) *t++ = 'i';
  if ((v->flags & VF_READONLY) != 0) *t++ = 'r';
  if ((v->flags & VF_EXPORT) != 0) *t++ = 'x';
  if ((v->flags & VF_LOWER) != 0) *t++ = 'l';
  if ((v->flags & VF_UPPER) != 0) *t++ = 'u';
  *t = '\0';
}

static void print_decl(const struct var_s *v)
{
  char fl[16];

  flag_letters(v, fl);
  printf("declare -%s %s", fl[0] != '\0' ? fl : "-", v->name);
  if (v->arr != NULL)
    {
      if ((v->flags & VF_NOVALUE) == 0)
        {
          putchar('=');
          vs_print_array_body(v->arr);
        }
    }
  else if (v->value != NULL)
    {
      putchar('=');
      vs_print_dq(v->value);
    }

  putchar('\n');
}

static int cmp_names(const void *a, const void *b)
{
  return strcmp((*(struct var_s *const *)a)->name, (*(struct var_s *const *)b)->name);
}

/* Every variable that has the attributes asked for, by name. */

static void list_vars(const struct dopts_s *o, enum decl_kind_e kind)
{
  struct var_s **vec;
  struct var_s *v;
  size_t n = 0;
  size_t k;

  for (v = g_sh.vars; v != NULL; v = v->next)
    {
      n++;
    }

  vec = vs_xmalloc((n + 1) * sizeof(*vec));
  n = 0;
  for (v = g_sh.vars; v != NULL; v = v->next)
    {
      bool keep = true;

      if (kind == DK_EXPORT || o->x)
        {
          keep = keep && (v->flags & VF_EXPORT) != 0;
        }

      if (kind == DK_READONLY || o->r)
        {
          keep = keep && (v->flags & VF_READONLY) != 0;
        }

      if (o->a)
        {
          keep = keep && v->arr != NULL && !v->arr->assoc;
        }

      if (o->A)
        {
          keep = keep && v->arr != NULL && v->arr->assoc;
        }

      if (o->i)
        {
          keep = keep && (v->flags & VF_INTEGER) != 0;
        }

      /* nothing that is only a computed or unset placeholder */

      if (keep && (v->value != NULL || v->arr != NULL || (v->flags & (VF_EXPORT | VF_READONLY)) != 0 ||
                   (kind == DK_DECLARE)))
        {
          vec[n++] = v;
        }
    }

  qsort(vec, n, sizeof(*vec), cmp_names);
  for (k = 0; k < n; k++)
    {
      print_decl(vec[k]);
    }

  free(vec);
}

/* ---- Applying -------------------------------------------------------------------- */

static int decl_one(enum decl_kind_e kind, const struct dopts_s *o, const char *arg, bool raw)
{
  const char *word = kind_word(kind);
  bool is_asg;
  char *name = asg_target_name(arg, &is_asg);
  struct var_s *v;
  bool local;
  bool had_arr;
  int status = 0;

  if (name == NULL)
    {
      vs_err("%s: `%s': not a valid identifier", word, arg);
      return 1;
    }

  local = kind == DK_LOCAL || (kind == DK_DECLARE && g_sh.func_depth > 0 && !o->g);
  if (local && g_sh.func_depth > 0)
    {
      var_local_declare(name, NULL, vs_feat(VF_LOCAL_INHERITS));
    }

  var_set_flags(name, 0);                     /* creates it if need be */
  v = var_lookup(name);
  if ((v->flags & VF_READONLY) != 0 && (is_asg || o->any))
    {
      vs_err("%s: readonly variable", name);
      free(name);
      return 1;
    }

  had_arr = v->arr != NULL;
  if (o->a)
    {
      if (v->arr != NULL && v->arr->assoc)
        {
          vs_err("%s: cannot convert associative to indexed array", name);
          free(name);
          return 1;
        }

      if (var_array(name, true) == NULL)
        {
          free(name);
          return 1;
        }
    }

  if (o->A && var_assoc(name, true) == NULL)
    {
      free(name);
      return 1;
    }

  v = var_lookup(name);
  if (o->i)  v->flags |= VF_INTEGER;
  if (o->ni) v->flags &= ~(unsigned)VF_INTEGER;
  if (o->l)  v->flags = (v->flags | VF_LOWER) & ~(unsigned)VF_UPPER;
  if (o->u)  v->flags = (v->flags | VF_UPPER) & ~(unsigned)VF_LOWER;
  if (o->nl) v->flags &= ~(unsigned)VF_LOWER;
  if (o->nu) v->flags &= ~(unsigned)VF_UPPER;
  if (o->x || (kind == DK_EXPORT && !o->nx)) v->flags |= VF_EXPORT;
  if (o->nx) v->flags &= ~(unsigned)VF_EXPORT;

  if (is_asg)
    {
      status = assign_apply_decl(arg, raw, o->A);
    }
  else if ((o->a || o->A) && !had_arr)
    {
      v = var_lookup(name);
      if (v != NULL && v->arr != NULL && v->arr->n == 0)
        {
          v->flags |= VF_NOVALUE;             /* declared, not yet assigned: prints without = */
        }
    }

  if (o->r || kind == DK_READONLY)
    {
      var_set_flags(name, VF_READONLY);
    }

  free(name);
  return status;
}

/* declare -p name... : one line each; a name that is not there is an error. */

static int print_named(const char *word, char *const *names, int n)
{
  int status = 0;
  int i;

  for (i = 0; i < n; i++)
    {
      struct var_s *v = var_lookup(names[i]);

      if (v == NULL)
        {
          vs_err("%s: %s: not found", word, names[i]);
          status = 1;
        }
      else
        {
          print_decl(v);
        }
    }

  return status;
}

static int cmp_strs(const void *a, const void *b)
{
  return strcmp(*(char *const *)a, *(char *const *)b);
}

/* declare -F: `declare -f name` for every function, sorted. */

static int list_functions(char *const *names, int n)
{
  int status = 0;
  int i;

  if (n > 0)
    {
      for (i = 0; i < n; i++)
        {
          if (func_find(names[i]) != NULL)
            {
              puts(names[i]);
            }
          else
            {
              status = 1;
            }
        }

      return status;
    }

  {
    struct func_s *f;
    char **vec;
    size_t cnt = 0;
    size_t k;

    for (f = g_sh.funcs; f != NULL; f = f->next)
      {
        cnt++;
      }

    vec = vs_xmalloc((cnt + 1) * sizeof(*vec));
    cnt = 0;
    for (f = g_sh.funcs; f != NULL; f = f->next)
      {
        vec[cnt++] = f->name;
      }

    qsort(vec, cnt, sizeof(*vec), cmp_strs);
    for (k = 0; k < cnt; k++)
      {
        printf("declare -f %s\n", vec[k]);
      }

    free(vec);
  }

  return 0;
}

static int decl_main(enum decl_kind_e kind, int argc, char **argv)
{
  const char *word = kind_word(kind);
  struct dopts_s o;
  int i = 1;
  int status = 0;
  bool listed = false;

  memset(&o, 0, sizeof(o));
  for (; i < argc; i++)
    {
      const char *a = argv[i];
      bool minus = a[0] == '-';
      const char *c;

      if (strcmp(a, "--") == 0)
        {
          i++;
          break;
        }

      if ((a[0] != '-' && a[0] != '+') || a[1] == '\0' || (g_sh.decl_raw >> i & 1ULL))
        {
          break;
        }

      for (c = a + 1; *c != '\0'; c++)
        {
          switch (*c)
            {
              case 'a': if (minus) o.a = true; else goto cannot_destroy; break;
              case 'A': if (minus) o.A = true; else goto cannot_destroy; break;
              case 'i': if (minus) o.i = true; else o.ni = true; break;
              case 'l': if (minus) o.l = true; else o.nl = true; break;
              case 'u': if (minus) o.u = true; else o.nu = true; break;
              case 'x': if (minus) o.x = true; else o.nx = true; break;
              case 'n':
                if (kind == DK_EXPORT && minus)
                  {
                    o.nx = true;                   /* export -n: no longer exported */
                  }
                else
                  {
                    vs_err("%s: -n: namerefs are not supported", word);
                    return 1;
                  }

                break;
              case 'r': if (minus) o.r = true; else { vs_err("%s: %s: readonly variable", word, "+r"); return 1; } break;
              case 'p': o.p = true; break;
              case 'g': o.g = true; break;
              case 'f': o.f = true; break;
              case 'F': o.F = true; break;
              case 't': break;                     /* the trace attribute: functions only */
              default:
                vs_err("%s: -%c: invalid option", word, *c);
                vs_err("%s: usage: %s [-aAfFgilnprtux] [-p] [name[=value] ...]", word, word);
                return 2;
            }

          o.any = o.any || (*c != 'p' && *c != 'g' && *c != 'F' && *c != 'f');
        }
    }

  if (o.F)
    {
      return list_functions(argv + i, argc - i);
    }

  if (o.f)
    {
      vs_err("%s: -f: functions are not supported here", word);
      return 1;
    }

  if (kind == DK_LOCAL && g_sh.func_depth == 0)
    {
      vs_err("local: can only be used in a function");
      return 1;
    }

  if (i >= argc)
    {
      /* no names: list */

      if (kind == DK_LOCAL)
        {
          return 0;
        }

      if (kind == DK_DECLARE && !o.p && !o.any)
        {
          const struct builtin_s *setb = builtin_find("set");
          char *sargv[2] = { "set", NULL };

          return setb->fn(1, sargv);            /* variables in `set` format */
        }

      list_vars(&o, kind);
      return 0;
    }

  if (o.p)
    {
      return print_named(word, argv + i, argc - i);
    }

  for (; i < argc; i++)
    {
      bool raw = i < 64 && ((g_sh.decl_raw >> i) & 1ULL) != 0;

      if (decl_one(kind, &o, argv[i], raw) != 0)
        {
          status = 1;
        }
    }

  (void)listed;
  return status;

cannot_destroy:
  vs_err("%s: cannot destroy array variables in this way", word);
  return 1;
}

int bi_declare(int argc, char **argv)
{
  return decl_main(DK_DECLARE, argc, argv);
}

int bi_local_decl(int argc, char **argv)
{
  return decl_main(DK_LOCAL, argc, argv);
}

int bi_export_decl(int argc, char **argv)
{
  return decl_main(DK_EXPORT, argc, argv);
}

int bi_readonly_decl(int argc, char **argv)
{
  return decl_main(DK_READONLY, argc, argv);
}
