/*
 * builtins_extra.c -- getopts, local, times and ulimit.
 */

#include <nuttx/config.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef VAPORSHELL_POSIX
#  include <sys/resource.h>
#  include <sys/times.h>
#  include <unistd.h>
#endif

#include "vaporshell.h"
#include "exec.h"
#include "mode.h"

/* ---- getopts --------------------------------------------------------------- */

static void set_opt_result(const char *name, const char *val, int optind_now)
{
  char buf[24];

  var_set(name, val);
  snprintf(buf, sizeof(buf), "%d", optind_now);
  var_set("OPTIND", buf);
  strcpy(g_sh.getopts_last, buf);
}

/* getopts optstring name [arg...]. OPTIND is the index of the next
 * argument to look at; when a script changes it (OPTIND=1 to rescan) the
 * position inside a cluster like -abc is forgotten.
 */

int bi_getopts(int argc, char **argv)
{
  const char *optstring;
  const char *name;
  char **args;
  int nargs;
  int optind_now = 1;
  const char *cur;
  bool silent;
  const char *quiet_var;
  char c;
  const char *spec;

  if (argc < 3)
    {
      vs_err("getopts: usage: getopts optstring name [arg ...]");
      return 2;
    }

  optstring = argv[1];
  name = argv[2];
  silent = optstring[0] == ':';
  if (silent)
    {
      optstring++;
    }

  if (argc > 3)
    {
      args = argv + 3;
      nargs = argc - 3;
    }
  else
    {
      args = g_sh.pos;
      nargs = g_sh.npos;
    }

  cur = var_get("OPTIND");
  if (cur != NULL)
    {
      optind_now = atoi(cur);
      if (strcmp(cur, g_sh.getopts_last) != 0)
        {
          g_sh.getopts_pos = 0;
        }
    }

  if (optind_now < 1)
    {
      optind_now = 1;
      g_sh.getopts_pos = 0;
    }

  quiet_var = var_get("OPTERR");

  /* Nothing left, or the next word is not an option. */

  if (optind_now > nargs || args[optind_now - 1][0] != '-' ||
      args[optind_now - 1][1] == '\0')
    {
      var_unset("OPTARG");
      g_sh.getopts_pos = 0;
      set_opt_result(name, "?", optind_now);
      return 1;
    }

  if (strcmp(args[optind_now - 1], "--") == 0)
    {
      var_unset("OPTARG");
      g_sh.getopts_pos = 0;
      set_opt_result(name, "?", optind_now + 1);
      return 1;
    }

  if (g_sh.getopts_pos == 0)
    {
      g_sh.getopts_pos = 1;
    }

  c = args[optind_now - 1][g_sh.getopts_pos];
  spec = (c == ':') ? NULL : strchr(optstring, c);
  {
    bool last = args[optind_now - 1][g_sh.getopts_pos + 1] == '\0';
    char cs[2] = { c, '\0' };

    if (spec == NULL)
      {
        /* unknown option */

        if (silent)
          {
            var_set("OPTARG", cs);
          }
        else
          {
            var_unset("OPTARG");
            if (quiet_var == NULL || strcmp(quiet_var, "0") != 0)
              {
                vs_err("illegal option -- %c", c);
              }
          }

        if (last)
          {
            optind_now++;
            g_sh.getopts_pos = 0;
          }
        else
          {
            g_sh.getopts_pos++;
          }

        set_opt_result(name, "?", optind_now);
        return 0;
      }

    if (spec[1] == ':')
      {
        /* takes an argument: the rest of this word, or the next word */

        if (!last)
          {
            var_set("OPTARG", args[optind_now - 1] + g_sh.getopts_pos + 1);
            optind_now++;
          }
        else if (optind_now < nargs)
          {
            var_set("OPTARG", args[optind_now]);
            optind_now += 2;
          }
        else
          {
            optind_now++;
            g_sh.getopts_pos = 0;
            if (silent)
              {
                var_set("OPTARG", cs);
                set_opt_result(name, ":", optind_now);
              }
            else
              {
                var_unset("OPTARG");
                if (quiet_var == NULL || strcmp(quiet_var, "0") != 0)
                  {
                    vs_err("option requires an argument -- %c", c);
                  }

                set_opt_result(name, "?", optind_now);
              }

            return 0;
          }

        g_sh.getopts_pos = 0;
        set_opt_result(name, cs, optind_now);
        return 0;
      }

    if (vs_feat(VF_OPTARG_EMPTY))
      {
        var_set("OPTARG", "");
      }
    else
      {
        var_unset("OPTARG");
      }

    if (last)
      {
        optind_now++;
        g_sh.getopts_pos = 0;
      }
    else
      {
        g_sh.getopts_pos++;
      }

    set_opt_result(name, cs, optind_now);
    return 0;
  }
}

/* ---- local ------------------------------------------------------------------ */

int bi_local(int argc, char **argv)
{
  int status = 0;
  int i;

  if (vs_feat(VF_BASH_SYNTAX))
    {
      return bi_local_decl(argc, argv);     /* attributes and arrays: declare.c */
    }

  if (g_sh.func_depth == 0)
    {
      vs_err("local: can only be used in a function");
      return 1;
    }

  for (i = 1; i < argc; i++)
    {
      const char *eq = strchr(argv[i], '=');
      size_t nlen = eq != NULL ? (size_t)(eq - argv[i]) : strlen(argv[i]);
      char *name;

      if (!is_valid_name(argv[i], nlen))
        {
          vs_err("local: `%s': not a valid identifier", argv[i]);
          status = 1;
          continue;
        }

      name = vs_xstrndup(argv[i], nlen);
      if (var_local_declare(name, eq != NULL ? eq + 1 : NULL,
                            vs_feat(VF_LOCAL_INHERITS)) != 0)
        {
          status = 1;
        }

      free(name);
    }

  return status;
}

/* ---- times ------------------------------------------------------------------ */

#ifdef VAPORSHELL_POSIX

static void print_time(clock_t t, long hz)
{
  long whole = (long)(t / (clock_t)hz);
  long frac = (long)(t % (clock_t)hz);

  if (vs_feat(VF_BASH_INFO_FORMATS))
    {
      printf("%ldm%ld.%03lds", whole / 60, whole % 60, frac * 1000 / hz);
    }
  else
    {
      printf("%ldm%ld.%06lds", whole / 60, whole % 60, frac * 1000000 / hz);
    }
}

int bi_times(int argc, char **argv)
{
  struct tms t;
  long hz = sysconf(_SC_CLK_TCK);

  (void)argc;
  (void)argv;
  if (times(&t) == (clock_t)-1 || hz <= 0)
    {
      vs_err("times: %s", strerror(errno));
      return 1;
    }

  print_time(t.tms_utime, hz);
  putchar(' ');
  print_time(t.tms_stime, hz);
  putchar('\n');
  print_time(t.tms_cutime, hz);
  putchar(' ');
  print_time(t.tms_cstime, hz);
  putchar('\n');
  return 0;
}

/* ---- ulimit ----------------------------------------------------------------- */

struct ulim_s
{
  char opt;
  int resource;
  long unit;                  /* rlim values are bytes; unit converts */
  const char *desc;
};

int bi_ulimit(int argc, char **argv)
{
  /* -c and -f count blocks: 1024 bytes in bash, 512 in dash (measured). */

  long blk = vs_feat(VF_BASH_INFO_FORMATS) ? 1024 : 512;
  const struct ulim_s table[] =
  {
    { 'c', RLIMIT_CORE,   blk,  "core file size" },
    { 'd', RLIMIT_DATA,   1024, "data seg size" },
    { 'f', RLIMIT_FSIZE,  blk,  "file size" },
    { 'n', RLIMIT_NOFILE, 1,    "open files" },
    { 's', RLIMIT_STACK,  1024, "stack size" },
    { 't', RLIMIT_CPU,    1,    "cpu time" },
#ifdef RLIMIT_AS
    { 'v', RLIMIT_AS,     1024, "address space" },
#endif
  };
  const struct ulim_s *sel = NULL;
  bool hard = false;
  bool soft = false;
  int i = 1;
  size_t k;
  struct rlimit rl;

  for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++)
    {
      const char *o;

      if (strcmp(argv[i], "--") == 0)
        {
          i++;
          break;
        }

      for (o = argv[i] + 1; *o != '\0'; o++)
        {
          if (*o == 'H')
            {
              hard = true;
            }
          else if (*o == 'S')
            {
              soft = true;
            }
          else
            {
              for (k = 0; k < sizeof(table) / sizeof(table[0]); k++)
                {
                  if (table[k].opt == *o)
                    {
                      sel = &table[k];
                      break;
                    }
                }

              if (k == sizeof(table) / sizeof(table[0]))
                {
                  vs_err("ulimit: -%c: invalid option", *o);
                  return 2;
                }
            }
        }
    }

  if (sel == NULL)
    {
      for (k = 0; k < sizeof(table) / sizeof(table[0]); k++)
        {
          if (table[k].opt == 'f')
            {
              sel = &table[k];
            }
        }
    }

  if (getrlimit(sel->resource, &rl) != 0)
    {
      vs_err("ulimit: %s", strerror(errno));
      return 1;
    }

  if (i >= argc)
    {
      rlim_t v = hard ? rl.rlim_max : rl.rlim_cur;

      if (v == RLIM_INFINITY)
        {
          puts("unlimited");
        }
      else
        {
          printf("%lu\n", (unsigned long)(v / (rlim_t)sel->unit));
        }

      return 0;
    }

  {
    rlim_t nv;

    if (strcmp(argv[i], "unlimited") == 0)
      {
        nv = RLIM_INFINITY;
      }
    else
      {
        char *end;
        unsigned long u = strtoul(argv[i], &end, 10);

        if (*argv[i] == '\0' || *end != '\0')
          {
            vs_err("ulimit: %s: invalid number", argv[i]);
            return 1;
          }

        nv = (rlim_t)u * (rlim_t)sel->unit;
      }

    /* No -H/-S: both limits are set. */

    if (hard || !soft)
      {
        rl.rlim_max = nv;
      }

    if (soft || !hard)
      {
        rl.rlim_cur = nv;
      }

    if (setrlimit(sel->resource, &rl) != 0)
      {
        vs_err("ulimit: %s: cannot modify limit: %s", sel->desc, strerror(errno));
        return 1;
      }
  }

  return 0;
}

#endif /* VAPORSHELL_POSIX */
