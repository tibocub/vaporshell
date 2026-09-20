/*
 * shopt.c -- the `shopt` builtin.
 *
 * bash has 59 options. Each is one of:
 *
 *   IMPL   implemented here (glob, matching and alias behaviour)
 *   MAP    a switch for a behaviour vaporshell already has as a mode feature
 *          (inherit_errexit, xpg_echo)
 *   NOOP   accepted and remembered so `shopt` reports it, but it changes
 *          nothing: interactive-shell settings (history, completion, prompts)
 *          that mean nothing to a script
 *   UNSUP  asking for it is an error rather than silently doing nothing, so
 *          a script that depends on it fails loudly (lastpipe, compat31...)
 *
 * The names, their order and their defaults are bash 5.3's (`bash -c shopt`
 * in a non-interactive shell).
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "exec.h"
#include "mode.h"

enum so_class_e
{
  SO_IMPL,
  SO_MAP,
  SO_NOOP,
  SO_UNSUP
};

enum so_id_e
{
  SO_NONE,
  SO_NULLGLOB,
  SO_DOTGLOB,
  SO_FAILGLOB,
  SO_NOCASEGLOB,
  SO_NOCASEMATCH,
  SO_EXTGLOB,
  SO_GLOBSTAR,
  SO_EXPAND_ALIASES,
  SO_PATSUB,
  SO_INHERIT_ERREXIT,
  SO_XPG_ECHO
};

static const struct
{
  const char *name;
  bool dflt;
  enum so_class_e cls;
  enum so_id_e id;
} g_so[] =
{
  { "array_expand_once", false, SO_UNSUP, SO_NONE },
  { "assoc_expand_once", false, SO_UNSUP, SO_NONE },
  { "autocd", false, SO_NOOP, SO_NONE },
  { "bash_source_fullpath", false, SO_NOOP, SO_NONE },
  { "cdable_vars", false, SO_UNSUP, SO_NONE },
  { "cdspell", false, SO_NOOP, SO_NONE },
  { "checkhash", false, SO_NOOP, SO_NONE },
  { "checkjobs", false, SO_NOOP, SO_NONE },
  { "checkwinsize", true, SO_NOOP, SO_NONE },
  { "cmdhist", true, SO_NOOP, SO_NONE },
  { "compat31", false, SO_UNSUP, SO_NONE },
  { "compat32", false, SO_UNSUP, SO_NONE },
  { "compat40", false, SO_UNSUP, SO_NONE },
  { "compat41", false, SO_UNSUP, SO_NONE },
  { "compat42", false, SO_UNSUP, SO_NONE },
  { "compat43", false, SO_UNSUP, SO_NONE },
  { "compat44", false, SO_UNSUP, SO_NONE },
  { "complete_fullquote", true, SO_NOOP, SO_NONE },
  { "direxpand", false, SO_NOOP, SO_NONE },
  { "dirspell", false, SO_NOOP, SO_NONE },
  { "dotglob", false, SO_IMPL, SO_DOTGLOB },
  { "execfail", false, SO_NOOP, SO_NONE },
  { "expand_aliases", false, SO_IMPL, SO_EXPAND_ALIASES },
  { "extdebug", false, SO_UNSUP, SO_NONE },
  { "extglob", false, SO_IMPL, SO_EXTGLOB },
  { "extquote", true, SO_NOOP, SO_NONE },
  { "failglob", false, SO_IMPL, SO_FAILGLOB },
  { "force_fignore", true, SO_NOOP, SO_NONE },
  { "globasciiranges", true, SO_NOOP, SO_NONE },
  { "globskipdots", true, SO_NOOP, SO_NONE },
  { "globstar", false, SO_IMPL, SO_GLOBSTAR },
  { "gnu_errfmt", false, SO_NOOP, SO_NONE },
  { "histappend", false, SO_NOOP, SO_NONE },
  { "histreedit", false, SO_NOOP, SO_NONE },
  { "histverify", false, SO_NOOP, SO_NONE },
  { "hostcomplete", true, SO_NOOP, SO_NONE },
  { "huponexit", false, SO_NOOP, SO_NONE },
  { "inherit_errexit", false, SO_MAP, SO_INHERIT_ERREXIT },
  { "interactive_comments", true, SO_NOOP, SO_NONE },
  { "lastpipe", false, SO_UNSUP, SO_NONE },
  { "lithist", false, SO_NOOP, SO_NONE },
  { "localvar_inherit", false, SO_UNSUP, SO_NONE },
  { "localvar_unset", false, SO_UNSUP, SO_NONE },
  { "login_shell", false, SO_UNSUP, SO_NONE },
  { "mailwarn", false, SO_NOOP, SO_NONE },
  { "no_empty_cmd_completion", false, SO_NOOP, SO_NONE },
  { "nocaseglob", false, SO_IMPL, SO_NOCASEGLOB },
  { "nocasematch", false, SO_IMPL, SO_NOCASEMATCH },
  { "noexpand_translation", false, SO_NOOP, SO_NONE },
  { "nullglob", false, SO_IMPL, SO_NULLGLOB },
  { "patsub_replacement", true, SO_IMPL, SO_PATSUB },
  { "progcomp", true, SO_NOOP, SO_NONE },
  { "progcomp_alias", false, SO_NOOP, SO_NONE },
  { "promptvars", true, SO_NOOP, SO_NONE },
  { "restricted_shell", false, SO_UNSUP, SO_NONE },
  { "shift_verbose", false, SO_NOOP, SO_NONE },
  { "sourcepath", true, SO_NOOP, SO_NONE },
  { "varredir_close", false, SO_NOOP, SO_NONE },
  { "xpg_echo", false, SO_MAP, SO_XPG_ECHO }
};

#define NSO ((int)(sizeof(g_so) / sizeof(g_so[0])))

static bool *impl_slot(enum so_id_e id)
{
  switch (id)
    {
      case SO_NULLGLOB:       return &g_sh.so_nullglob;
      case SO_DOTGLOB:        return &g_sh.so_dotglob;
      case SO_FAILGLOB:       return &g_sh.so_failglob;
      case SO_NOCASEGLOB:     return &g_sh.so_nocaseglob;
      case SO_NOCASEMATCH:    return &g_sh.so_nocasematch;
      case SO_EXTGLOB:        return &g_sh.so_extglob;
      case SO_GLOBSTAR:       return &g_sh.so_globstar;
      case SO_EXPAND_ALIASES: return &g_sh.so_expand_aliases;
      case SO_PATSUB:         return &g_sh.so_patsub;
      default:                return NULL;
    }
}

/* Called once by shell_init(): every option at bash's default. */

void vs_shopt_defaults(void)
{
  int i;

  for (i = 0; i < NSO; i++)
    {
      if (g_so[i].cls == SO_IMPL)
        {
          *impl_slot(g_so[i].id) = g_so[i].dflt;
        }
      else if (g_so[i].cls == SO_NOOP && i < (int)sizeof(g_sh.so_generic))
        {
          g_sh.so_generic[i] = g_so[i].dflt;
        }
    }
}

static bool so_get(int i)
{
  switch (g_so[i].cls)
    {
      case SO_IMPL:
        return *impl_slot(g_so[i].id);

      case SO_MAP:
        return g_so[i].id == SO_INHERIT_ERREXIT ? vs_feat(VF_ERREXIT_IN_CMDSUB)
                                                : vs_feat(VF_ECHO_XPG);

      case SO_NOOP:
        return g_sh.so_generic[i] != 0;

      default:
        return g_so[i].dflt;
    }
}

/* 0 on success; 1 if the option cannot be changed (and says so). */

static int so_set(int i, bool on)
{
  switch (g_so[i].cls)
    {
      case SO_IMPL:
        *impl_slot(g_so[i].id) = on;
        return 0;

      case SO_MAP:
        {
          unsigned long bit = 1UL << (unsigned)(g_so[i].id == SO_INHERIT_ERREXIT
                                                ? VF_ERREXIT_IN_CMDSUB
                                                : VF_ECHO_XPG);

          if (on)
            {
              g_sh.features |= bit;
            }
          else
            {
              g_sh.features &= ~bit;
            }

          return 0;
        }

      case SO_NOOP:
        g_sh.so_generic[i] = on ? 1 : 0;
        return 0;

      default:
        if (on == g_so[i].dflt)
          {
            return 0;                      /* already how it is: nothing to do */
          }

        vs_err("shopt: %s: not implemented", g_so[i].name);
        return 1;
    }
}

static int so_find(const char *name)
{
  int i;

  for (i = 0; i < NSO; i++)
    {
      if (strcmp(g_so[i].name, name) == 0)
        {
          return i;
        }
    }

  return -1;
}

int bi_shopt(int argc, char **argv)
{
  bool set = false;
  bool unset = false;
  bool quiet = false;
  bool print = false;
  bool oflag = false;
  int status = 0;
  int i = 1;

  for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++)
    {
      const char *f;

      if (strcmp(argv[i], "--") == 0)
        {
          i++;
          break;
        }

      for (f = argv[i] + 1; *f != '\0'; f++)
        {
          switch (*f)
            {
              case 's': set = true; break;
              case 'u': unset = true; break;
              case 'q': quiet = true; break;
              case 'p': print = true; break;
              case 'o': oflag = true; break;
              default:
                vs_err("shopt: -%c: invalid option", *f);
                vs_err("shopt: usage: shopt [-pqsu] [-o] [optname ...]");
                return 2;
            }
        }
    }

  if (set && unset)
    {
      vs_err("shopt: cannot set and unset shell options simultaneously");
      return 1;
    }

  if (oflag)
    {
      /* the `set -o` options: shopt -s -o name  is  set -o name, and so on */

      const struct builtin_s *setb = builtin_find("set");
      char *sargv[4];
      int n;

      if (i >= argc)
        {
          sargv[0] = "set";
          sargv[1] = print ? "+o" : "-o";
          sargv[2] = NULL;
          return setb->fn(2, sargv);
        }

      for (; i < argc; i++)
        {
          int st = vs_option_state(argv[i]);

          if (st < 0)
            {
              vs_err("shopt: %s: invalid option name", argv[i]);
              status = 1;
              continue;
            }

          if (set || unset)
            {
              sargv[0] = "set";
              sargv[1] = set ? "-o" : "+o";
              sargv[2] = argv[i];
              sargv[3] = NULL;
              n = setb->fn(3, sargv);
              status = n != 0 ? n : status;
            }
          else if (!quiet)
            {
              if (print)
                {
                  printf("set %co %s\n", st ? '-' : '+', argv[i]);
                }
              else
                {
                  printf("%-20s\t%s\n", argv[i], st ? "on" : "off");
                }
            }

          if (st == 0 && !set && !unset)
            {
              status = 1;
            }
        }

      return status;
    }

  if (i >= argc)
    {
      int k;

      for (k = 0; k < NSO; k++)
        {
          bool on = so_get(k);

          if ((set && !on) || (unset && on))
            {
              continue;
            }

          if (quiet)
            {
              continue;
            }

          if (print)
            {
              printf("shopt -%c %s\n", on ? 's' : 'u', g_so[k].name);
            }
          else
            {
              printf("%-20s\t%s\n", g_so[k].name, on ? "on" : "off");
            }
        }

      return 0;
    }

  for (; i < argc; i++)
    {
      int k = so_find(argv[i]);

      if (k < 0)
        {
          vs_err("shopt: %s: invalid shell option name", argv[i]);
          status = 1;
          continue;
        }

      if (set || unset)
        {
          if (so_set(k, set) != 0)
            {
              status = 1;
            }

          continue;
        }

      if (!so_get(k))
        {
          status = 1;
        }

      if (quiet)
        {
          continue;
        }

      if (print)
        {
          printf("shopt -%c %s\n", so_get(k) ? 's' : 'u', g_so[k].name);
        }
      else
        {
          printf("%-20s\t%s\n", g_so[k].name, so_get(k) ? "on" : "off");
        }
    }

  return status;
}
