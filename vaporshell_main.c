/*
 * vaporshell_main.c -- entry point.
 *
 *   vaporshell                       interactive (or commands from stdin)
 *   vaporshell -c cmd [name [args]]  run one command string
 *   vaporshell script [args]         run a script file
 *
 * Leading options: -e -u -x -f -C (see `set`), -i (interactive), -o/+o NAME,
 * --posix, and -- to end them.
 *
 * The language mode (mode.h) is bash's by default. POSIX mode is chosen by
 * --posix, -o posix, being invoked as `sh`, or POSIXLY_CORRECT in the
 * environment -- the same rules bash uses.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vaporshell.h"
#include "exec.h"
#include "mode.h"
#include "platform.h"
#include "complete.h"
#include "history.h"
#include "ledit.h"
#include "prompt.h"
#include "rcfile.h"
#include "version.h"

/* The line editor when standard input is a terminal; else (a forced -i with
 * a pipe) the prompt is written and a plain line read.
 */

static char *read_prompted(const char *prompt, bool continuation)
{
  char *plain;
  ssize_t n;

  if (vash_ledit_usable())
    {
      return vash_ledit_read(prompt, continuation);
    }

  plain = vash_xstrdup(prompt);
  vash_prompt_strip(plain);

  /* Prompts go to stderr on a host, like other shells, so stdout stays clean. */

#ifdef VAPORSHELL_POSIX
  n = write(STDERR_FILENO, plain, strlen(plain));
#else
  n = write(STDOUT_FILENO, plain, strlen(plain));
#endif
  (void)n;
  free(plain);
  return read_stream_line(stdin);
}

#ifdef VAPORSHELL_POSIX
#  define DEFAULT_PS1 "\\u@\\h:\\w\\$ "
#else
#  define DEFAULT_PS1 "vaporshell$ "      /* NuttX has no users: uid is always 0, so \$ would say # */
#endif

static char *interactive_next_line(void *ctx, bool continuation)
{
  const char *ps;
  char *expanded;
  char *line;

  (void)ctx;
  if (!continuation)
    {
      const char *pc = var_get("PROMPT_COMMAND");

      if (pc != NULL && *pc != '\0')
        {
          int saved = g_sh.last_status;

          run_string(pc, strlen(pc));
          g_sh.last_status = saved;
        }

      g_sh.cmd_num++;
    }

  ps = var_get(continuation ? "PS2" : "PS1");
  if (ps == NULL)
    {
      ps = continuation ? "> " : DEFAULT_PS1;
    }

  expanded = vash_prompt_expand(ps);
  line = read_prompted(expanded, continuation);
  free(expanded);
  return line;
}

static char *stdin_next_line(void *ctx, bool continuation)
{
  (void)ctx;
  (void)continuation;
  return read_stream_line(stdin);
}

static int usage_error(const char *what, const char *arg)
{
  vash_err("%s: %s", arg, what);
  return 2;
}

/* The shell's exit status; runs the EXIT trap first. */

static int finish(int status)
{
  g_sh.last_status = g_sh.unwind != UW_NONE ? g_sh.last_status : status;
  g_sh.unwind = UW_NONE;
  trap_run_exit();
  return g_sh.last_status & 0xff;
}

/* Variables bash always has and rc files test: $BASH (the shell's own path) and
 * SHLVL (nesting depth, exported).
 */

#define BASH_DEFAULT_PS1 "\\s-\\v\\$ "

static void bash_defaults(const char *argv0, bool prompting)
{
  char path[VASH_PATH_MAX];
  char lvl[16];
  const char *old = var_get("SHLVL");
  long n = old != NULL ? strtol(old, NULL, 10) : 0;
  ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);

  if (len > 0)
    {
      path[len] = '\0';
      argv0 = path;
    }

  if (prompting)
    {
      /* rc files (and /etc/bashrc) test PS1 to know they are in a prompting shell */

      if (var_get("PS1") == NULL)
        {
          var_set("PS1", BASH_DEFAULT_PS1);       /* what /etc/bashrc looks for to install its own */
        }

      if (var_get("PS2") == NULL)
        {
          var_set("PS2", "> ");
        }
    }

  if (var_get("PS4") == NULL)
    {
      var_set("PS4", "+ ");
    }

  g_sh.opt_noH = !prompting;                 /* history expansion is an interactive-shell default */

  if (var_get("COMP_WORDBREAKS") == NULL)
    {
      var_set("COMP_WORDBREAKS", " \t\n\"'@><=;|&(:");
    }

  var_set("BASH", argv0);
  snprintf(lvl, sizeof(lvl), "%ld", n < 0 || n > 998 ? 0 : n + 1);
  var_set("SHLVL", lvl);
  var_set_flags("SHLVL", VF_EXPORT);
}

static int shell_main(int argc, char *argv[])
{
  const char *command = NULL;
  bool have_command = false;
  bool force_interactive = false;
  bool login = false;
  bool norc = false;
  const char *rcfile = NULL;
  int i = 1;
  int status;

  shell_init("vaporshell");

  {
    const char *base = strrchr(argv[0], '/');

    base = (base != NULL) ? base + 1 : argv[0];
    if (strcmp(base, "sh") == 0 || var_get("POSIXLY_CORRECT") != NULL)
      {
        vash_mode_set(VASH_PROFILE_POSIX);
      }
  }

  for (; i < argc && (argv[i][0] == '-' || argv[i][0] == '+') &&
         argv[i][1] != '\0'; i++)
    {
      const char *f;

      if (strcmp(argv[i], "--") == 0)
        {
          i++;
          break;
        }

      if (strcmp(argv[i], "--version") == 0)
        {
          printf("vaporshell, version %s\n", VAPORSHELL_VERSION);
          return 0;
        }

      if (strcmp(argv[i], "--login") == 0)
        {
          login = true;
          continue;
        }

      if (strcmp(argv[i], "--norc") == 0)
        {
          norc = true;
          continue;
        }

      if (strcmp(argv[i], "--rcfile") == 0 || strcmp(argv[i], "--init-file") == 0)
        {
          if (i + 1 >= argc)
            {
              return usage_error("option requires an argument", argv[i]);
            }

          rcfile = argv[++i];
          continue;
        }

      if (strcmp(argv[i], "--posix") == 0)
        {
          vash_mode_set(VASH_PROFILE_POSIX);
          continue;
        }

      if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "+o") == 0)
        {
          if (i + 1 >= argc ||
              vash_set_named_option(argv[i + 1], argv[i][0] == '-') != 0)
            {
              return usage_error("invalid option name", argv[i]);
            }

          i++;
          continue;
        }

      if (argv[i][0] == '+')
        {
          return usage_error("invalid option", argv[i]);
        }

      for (f = argv[i] + 1; *f != '\0'; f++)
        {
          switch (*f)
            {
              case 'c': have_command = true; break;
              case 'i': force_interactive = true; g_sh.opt_i = true; break;
              case 'l': login = true; break;
              case 'e': g_sh.opt_e = true; break;
              case 'u': g_sh.opt_u = true; break;
              case 'x': g_sh.opt_x = true; break;
              case 'f': g_sh.opt_f = true; break;
              case 'C': g_sh.opt_C = true; break;
              case 'n': g_sh.opt_n = true; break;
              case 'v': g_sh.opt_v = true; break;
              default:  return usage_error("invalid option", argv[i]);
            }
        }
    }

  if (g_sh.profile != VASH_PROFILE_POSIX)
    {
      bash_defaults(argv[0], force_interactive || (!have_command && i >= argc && vash_plat_interactive()));
    }

  if (have_command)
    {
      if (i >= argc)
        {
          return usage_error("option requires an argument", "-c");
        }

      command = argv[i++];
      if (i < argc)
        {
          g_sh.arg0 = argv[i++];
        }

      pos_set(argv + i, argc - i);
      g_sh.cur_src = g_sh.arg0;             /* -c: bash reports $0 as the source */
      status = run_string(command, strlen(command));
      return finish(status);
    }

  if (i < argc)
    {
      g_sh.arg0 = argv[i];
      g_sh.cur_src = argv[i];
      g_sh.cur_script = argv[i];
      g_sh.script_main = true;             /* FUNCNAME ends with "main" for a script */
      pos_set(argv + i + 1, argc - i - 1);
      status = run_file(argv[i]);
      return finish(status);
    }

  {
    struct parser_s p;

    g_sh.interactive = force_interactive || vash_plat_interactive();
    if (g_sh.interactive)
      {
        vash_hist_init();
        if (g_sh.profile != VASH_PROFILE_POSIX)
          {
            const char *ps1;

            g_sh.so_expand_aliases = true;                  /* bash turns it on when interactive */
            g_sh.so_login_shell = login || argv[0][0] == '-';
            vash_rc_load(g_sh.so_login_shell, norc, rcfile);
            ps1 = var_get("PS1");
            if (ps1 != NULL && strcmp(ps1, BASH_DEFAULT_PS1) == 0)
              {
                var_set("PS1", DEFAULT_PS1);              /* nothing chose one: use ours */
              }
          }
      }

    parser_init(&p, g_sh.interactive ? interactive_next_line : stdin_next_line,
                NULL);
    status = run_source(&p, g_sh.interactive);
    parser_free(&p);
    if (g_sh.interactive)
      {
        vash_hist_save();
      }

    if (g_sh.interactive && g_sh.unwind == UW_NONE)
      {
        /* Ctrl+D: finish the prompt line. */

#ifdef VAPORSHELL_POSIX
        fputc('\n', stderr);
#else
        printf("\n");
#endif
      }
  }

  return finish(status);
}

int main(int argc, char *argv[])
{
  int rc = shell_main(argc, argv);

  shell_fini();
  return rc;
}
