/*
 * special.c -- bash's computed variables: PIPESTATUS, FUNCNAME, BASH_SOURCE,
 * BASH_LINENO and BASH_VERSINFO (BASH_REMATCH is set where =~ runs, in exec.c).
 *
 * They change far more often than they are read, so nothing is stored in the
 * variable table as it happens. The executor only records the raw facts (the
 * statuses of the last pipeline, the stack of function/source frames) and
 * flags them dirty; the arrays are built the first time somebody looks the
 * name up (var_lookup calls special_refresh).
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "mode.h"

#define LZ_PS       1u
#define LZ_FRAMES   2u
#define LZ_VERSINFO 4u

/* ---- recording ---------------------------------------------------------------------- */

/* A pipeline of n stages is done; stage i finished with 'status'. Stage 0
 * starts a new list.
 */

void ps_record(int i, int status)
{
  if (i == 0)
    {
      g_sh.nps = 0;
    }

  if (g_sh.nps < (int)(sizeof(g_sh.ps) / sizeof(g_sh.ps[0])))
    {
      g_sh.ps[g_sh.nps++] = status;
    }

  g_sh.lazy_dirty |= LZ_PS;
}

/* Every completed command is a one-stage pipeline. */

void ps_single(int status)
{
  g_sh.ps[0] = status;
  g_sh.nps = 1;
  g_sh.lazy_dirty |= LZ_PS;
}

/* 'f' lives on the caller's stack; it is linked in for the duration of a
 * function call or a sourced file.
 */

void frame_push(struct frame_s *f, const char *name, const char *src, int line, bool is_func)
{
  f->name = vs_xstrdup(name);
  f->src = vs_xstrdup(src != NULL ? src : "");
  f->line = line;
  f->is_func = is_func;
  f->up = g_sh.frame;
  g_sh.frame = f;
  g_sh.lazy_dirty |= LZ_FRAMES;
}

void frame_pop(struct frame_s *f)
{
  g_sh.frame = f->up;
  free(f->name);
  free(f->src);
  g_sh.lazy_dirty |= LZ_FRAMES;
}

/* ---- building ----------------------------------------------------------------------- */

static void set_array(const char *name, char **vals, int n)
{
  struct arr_s *a;
  int i;

  if (n == 0)
    {
      struct var_s *v = var_lookup(name);

      if (v != NULL)
        {
          v->flags &= ~(unsigned)VF_READONLY;
          var_unset(name);
        }

      return;
    }

  a = arr_new();
  for (i = 0; i < n; i++)
    {
      arr_set(a, i, vals[i]);
    }

  var_array_replace(name, a);
}

static void build_frames(void)
{
  struct frame_s *f;
  int n = 0;
  bool any_func = false;
  char **names;
  char **srcs;
  char **lines;
  int i = 0;

  for (f = g_sh.frame; f != NULL; f = f->up)
    {
      n++;
      any_func = any_func || f->is_func;
    }

  if (g_sh.script_main)
    {
      n++;                              /* the script itself: "main" */
    }

  names = vs_xmalloc((size_t)(n + 1) * sizeof(char *));
  srcs = vs_xmalloc((size_t)(n + 1) * sizeof(char *));
  lines = vs_xmalloc((size_t)(n + 1) * sizeof(char *));
  for (f = g_sh.frame; f != NULL; f = f->up)
    {
      char buf[24];

      snprintf(buf, sizeof(buf), "%d", f->line);
      names[i] = f->name;
      srcs[i] = f->src;
      lines[i] = vs_xstrdup(buf);
      i++;
    }

  if (g_sh.script_main)
    {
      names[i] = "main";
      srcs[i] = (char *)(g_sh.cur_script != NULL ? g_sh.cur_script : "");
      lines[i] = vs_xstrdup("0");
    }

  /* FUNCNAME only exists inside a function; the other two whenever there is
   * anything on the stack.
   */

  set_array("FUNCNAME", names, any_func ? n : 0);
  set_array("BASH_SOURCE", srcs, n);
  set_array("BASH_LINENO", lines, n);
  for (i = 0; i < n; i++)
    {
      free(lines[i]);
    }

  free(names);
  free(srcs);
  free(lines);
}

static void build_pipestatus(void)
{
  struct arr_s *a = arr_new();
  int i;

  for (i = 0; i < g_sh.nps; i++)
    {
      char buf[24];

      snprintf(buf, sizeof(buf), "%d", g_sh.ps[i]);
      arr_set(a, i, buf);
    }

  var_array_replace("PIPESTATUS", a);
}

static void build_versinfo(void)
{
  struct arr_s *a = arr_new();
  const char *mt = var_get("MACHTYPE");

  arr_set(a, 0, "5");
  arr_set(a, 1, "3");
  arr_set(a, 2, "0");
  arr_set(a, 3, "1");
  arr_set(a, 4, "release");
  arr_set(a, 5, mt != NULL ? mt : "unknown-unknown-vaporshell");
  var_array_replace("BASH_VERSINFO", a);
  var_set_flags("BASH_VERSINFO", VF_READONLY);
}

/* var_lookup(name) is about to search: bring 'name' up to date if it is one of
 * ours. The bit is cleared first, because building calls var_lookup itself.
 */

void special_refresh(const char *name)
{
  if (!vs_feat(VF_BASH_VARS))
    {
      g_sh.lazy_dirty = 0;
      return;
    }

  if ((g_sh.lazy_dirty & LZ_PS) != 0 && strcmp(name, "PIPESTATUS") == 0)
    {
      g_sh.lazy_dirty &= ~LZ_PS;
      build_pipestatus();
    }
  else if ((g_sh.lazy_dirty & LZ_FRAMES) != 0 &&
           (strcmp(name, "FUNCNAME") == 0 || strcmp(name, "BASH_SOURCE") == 0 ||
            strcmp(name, "BASH_LINENO") == 0))
    {
      g_sh.lazy_dirty &= ~LZ_FRAMES;
      build_frames();
    }
  else if ((g_sh.lazy_dirty & LZ_VERSINFO) != 0 && strcmp(name, "BASH_VERSINFO") == 0)
    {
      g_sh.lazy_dirty &= ~LZ_VERSINFO;
      build_versinfo();
    }
}

void special_init(void)
{
  g_sh.nps = 0;                         /* empty until a command has run */
  g_sh.lazy_dirty = LZ_FRAMES | LZ_VERSINFO;
}
