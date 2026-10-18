/*
 * compfish.c -- fish-style completion specs, the data format fish's own
 * completions use, so a spec reads the same here:
 *
 *   complete -c git -s h -l help -d 'Show help'
 *   complete -c git -n '[[ ${#COMP_WORDS[@]} -le 2 ]]' -a 'add commit push' -d 'Command'
 *   complete -c ssh -x -a 'host1 host2'
 *
 * -c/--command CMD   the command            -s/--short-option X   -X
 * -l/--long-option N  --N                   -o/--old-option N     -N
 * -d/--description T  shown next to it      -a/--arguments W      words (word<TAB>text)
 * -n/--condition C    shell code; the spec applies when it succeeds (COMP_WORDS,
 *                     COMP_CWORD, COMP_LINE, COMP_POINT are set)
 * -f/--no-files  -x/--exclusive  -r/--require-parameter  -F/--force-files
 * -e/--erase         remove every spec of the command
 *
 * Not fish's: -a takes words, not command substitutions; no -k, --wraps, --keep-order.
 * A spec is a few strings; nothing is read or run until the command is completed.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "exec.h"
#include "compfish.h"

struct fspec_s
{
  struct fspec_s *next;
  char *cmd;
  char *shortopt;               /* the letter */
  char *longopt;                /* "--name" */
  char *oldopt;                 /* "-name" */
  char *desc;
  char *args;
  char *cond;
  bool no_files;
};

static void fspec_free(struct fspec_s *f)
{
  free(f->cmd);
  free(f->shortopt);
  free(f->longopt);
  free(f->oldopt);
  free(f->desc);
  free(f->args);
  free(f->cond);
  free(f);
}

/* The long and short spellings of the flags this accepts, as one table. */

static const struct
{
  const char *brief;
  const char *lng;
  bool value;
} g_flags[] =
{
  { "-c", "--command", true },        { "-s", "--short-option", true },
  { "-l", "--long-option", true },    { "-o", "--old-option", true },
  { "-d", "--description", true },    { "-a", "--arguments", true },
  { "-n", "--condition", true },      { "-f", "--no-files", false },
  { "-x", "--exclusive", false },     { "-r", "--require-parameter", false },
  { "-F", "--force-files", false },   { "-e", "--erase", false },
};

#define NFLAGS ((int)(sizeof(g_flags) / sizeof(g_flags[0])))

static int flag_index(const char *a)
{
  int i;

  for (i = 0; i < NFLAGS; i++)
    {
      if (strcmp(a, g_flags[i].brief) == 0 || strcmp(a, g_flags[i].lng) == 0)
        {
          return i;
        }
    }

  return -1;
}

/* bash's `-o` takes one of these; anything else after -o is a fish old-style option. */

static bool bash_o_value(const char *v)
{
  static const char *const words[] =
  {
    "default", "dirnames", "filenames", "noquote", "nosort", "nospace", "plusdirs", "bashdefault"
  };
  size_t i;

  for (i = 0; i < sizeof(words) / sizeof(words[0]); i++)
    {
      if (strcmp(v, words[i]) == 0)
        {
          return true;
        }
    }

  return false;
}

static char *join(const char *dashes, const char *name)
{
  char *r = vs_xmalloc(strlen(dashes) + strlen(name) + 1);

  sprintf(r, "%s%s", dashes, name);
  return r;
}

int vs_complete_fish(int argc, char **argv)
{
  /* It is fish-style if --command is used, or -c CMD comes with a flag only fish
   * has (bash's complete has -c as "command names" with no value).
   */

  bool fish = false;
  bool has_c = false;
  bool erase = false;
  struct fspec_s *f;
  int i;

  for (i = 1; i < argc; i++)
    {
      int k = flag_index(argv[i]);

      if (k == 0 && i + 1 < argc && argv[i + 1][0] != '-')
        {
          has_c = true;
          fish = fish || strcmp(argv[i], "--command") == 0;
        }
      else if (k >= 0 && strchr("slnrdxe", g_flags[k].brief[1]) != NULL &&
               !(k == 3 && i + 1 < argc && bash_o_value(argv[i + 1])))
        {
          fish = true;
        }
    }

  if (!fish || !has_c)
    {
      return -1;
    }

  f = vs_xmalloc(sizeof(*f));
  memset(f, 0, sizeof(*f));
  for (i = 1; i < argc; i++)
    {
      int k = flag_index(argv[i]);
      const char *v = k >= 0 && g_flags[k].value && i + 1 < argc ? argv[i + 1] : NULL;

      if (k < 0 || (g_flags[k].value && v == NULL))
        {
          vs_err("complete: %s: invalid option", argv[i]);
          fspec_free(f);
          return 2;
        }

      i += v != NULL;
      switch (k)
        {
          case 0:  free(f->cmd); f->cmd = vs_xstrdup(v); break;
          case 1:  free(f->shortopt); f->shortopt = join("-", v); break;
          case 2:  free(f->longopt); f->longopt = join("--", v); break;
          case 3:  free(f->oldopt); f->oldopt = join("-", v); break;
          case 4:  free(f->desc); f->desc = vs_xstrdup(v); break;
          case 5:  free(f->args); f->args = vs_xstrdup(v); break;
          case 6:  free(f->cond); f->cond = vs_xstrdup(v); break;
          case 7:
          case 8:  f->no_files = true; break;
          case 11: erase = true; break;
          default: break;
        }
    }

  if (f->cmd == NULL)
    {
      fspec_free(f);
      return 2;
    }

  if (erase)
    {
      struct fspec_s **p = &g_sh.fspecs;

      while (*p != NULL)
        {
          if (strcmp((*p)->cmd, f->cmd) == 0)
            {
              struct fspec_s *dead = *p;

              *p = dead->next;
              fspec_free(dead);
            }
          else
            {
              p = &(*p)->next;
            }
        }

      fspec_free(f);
      return 0;
    }

  /* Appended, so completions list in the order they were declared. */

  {
    struct fspec_s **p = &g_sh.fspecs;

    while (*p != NULL)
      {
        p = &(*p)->next;
      }

    *p = f;
  }

  return 0;
}

bool vs_fish_known(const char *cmd)
{
  const struct fspec_s *f;

  for (f = g_sh.fspecs; f != NULL; f = f->next)
    {
      if (strcmp(f->cmd, cmd) == 0)
        {
          return true;
        }
    }

  return false;
}

/* An empty spec: "this command's completion file was run already". */

void vs_fish_mark(const char *cmd)
{
  struct fspec_s *f = vs_xmalloc(sizeof(*f));

  memset(f, 0, sizeof(*f));
  f->cmd = vs_xstrdup(cmd);
  f->next = g_sh.fspecs;
  g_sh.fspecs = f;
}

void vs_fish_free(void)
{
  while (g_sh.fspecs != NULL)
    {
      struct fspec_s *f = g_sh.fspecs;

      g_sh.fspecs = f->next;
      fspec_free(f);
    }
}

static bool condition_holds(const char *cond)
{
  int saved = g_sh.last_status;
  int status = run_string(cond, strlen(cond));

  g_sh.last_status = saved;
  g_sh.unwind = UW_NONE;
  return status == 0;
}

/* -a "one two\tabout it three": words separated by blanks, a word's own text after a tab. */

static void each_arg(const struct fspec_s *f, vs_comp_list_f sink, void *ctx)
{
  char *copy = vs_xstrdup(f->args);
  char *p = copy;

  while (*p != '\0')
    {
      char *word;
      char *tab;

      while (*p == ' ' || *p == '\n')
        {
          p++;
        }

      word = p;
      while (*p != '\0' && *p != ' ' && *p != '\n')
        {
          p++;
        }

      if (*p != '\0')
        {
          *p++ = '\0';
        }

      tab = strchr(word, '\t');
      if (tab != NULL)
        {
          *tab = '\0';
        }

      if (*word != '\0')
        {
          sink(ctx, word, tab != NULL ? tab + 1 : f->desc);
        }
    }

  free(copy);
}

int vs_comp_fish(const char *cmd, const char *line, int point, const char *word,
                 vs_comp_list_f sink, void *ctx)
{
  const struct fspec_s *f;
  int result = 0;
  bool vars_set = false;

  for (f = g_sh.fspecs; f != NULL; f = f->next)
    {
      if (strcmp(f->cmd, cmd) != 0)
        {
          continue;
        }

      result |= VS_SPEC_FOUND;
      if (f->cond != NULL)
        {
          if (!vars_set)
            {
              vs_set_comp_vars(line, point);
              vars_set = true;
            }

          if (!condition_holds(f->cond))
            {
              continue;
            }
        }

      if (f->no_files)
        {
          result |= VS_SPEC_EXCLUSIVE;
        }

      if (word[0] == '-')
        {
          if (f->shortopt != NULL)
            {
              sink(ctx, f->shortopt, f->desc);
            }

          if (f->longopt != NULL)
            {
              sink(ctx, f->longopt, f->desc);
            }

          if (f->oldopt != NULL)
            {
              sink(ctx, f->oldopt, f->desc);
            }
        }
      else if (f->args != NULL)
        {
          each_arg(f, sink, ctx);
        }
    }

  return result;
}
