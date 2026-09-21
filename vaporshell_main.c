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

#include "system/readline.h"

#include "vaporshell.h"
#include "exec.h"
#include "mode.h"
#include "platform.h"

/* readline() never prints the prompt itself (confirmed against NuttX's
 * readline_fd.c): write it first, then tell readline what to redraw.
 */

static char *read_prompted(const char *prompt)
{
#ifdef VAPORSHELL_POSIX
  /* Prompts go to stderr, like other shells, so stdout stays clean. */

  ssize_t n = write(STDERR_FILENO, prompt, strlen(prompt));
#else
  ssize_t n = write(STDOUT_FILENO, prompt, strlen(prompt));
#endif

  (void)n;
#if defined(CONFIG_READLINE_TABCOMPLETION) || defined(CONFIG_READLINE_EDIT)
  readline_prompt(prompt);
#endif
  return readline(prompt);
}

static char *interactive_next_line(void *ctx, bool continuation)
{
  const char *ps = continuation ? var_get("PS2") : var_get("PS1");

  (void)ctx;
  if (ps == NULL)
    {
      ps = continuation ? "> " : "vaporshell$ ";
    }

  return read_prompted(ps);
}

static char *stdin_next_line(void *ctx, bool continuation)
{
  (void)ctx;
  (void)continuation;
  return read_stream_line(stdin);
}

static int usage_error(const char *what, const char *arg)
{
  vs_err("%s: %s", arg, what);
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

static int shell_main(int argc, char *argv[])
{
  const char *command = NULL;
  bool have_command = false;
  bool force_interactive = false;
  int i = 1;
  int status;

  shell_init("vaporshell");

  {
    const char *base = strrchr(argv[0], '/');

    base = (base != NULL) ? base + 1 : argv[0];
    if (strcmp(base, "sh") == 0 || var_get("POSIXLY_CORRECT") != NULL)
      {
        vs_mode_set(VS_PROFILE_POSIX);
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

      if (strcmp(argv[i], "--posix") == 0)
        {
          vs_mode_set(VS_PROFILE_POSIX);
          continue;
        }

      if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "+o") == 0)
        {
          if (i + 1 >= argc ||
              vs_set_named_option(argv[i + 1], argv[i][0] == '-') != 0)
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
              case 'i': force_interactive = true; break;
              case 'e': g_sh.opt_e = true; break;
              case 'u': g_sh.opt_u = true; break;
              case 'x': g_sh.opt_x = true; break;
              case 'f': g_sh.opt_f = true; break;
              case 'C': g_sh.opt_C = true; break;
              default:  return usage_error("invalid option", argv[i]);
            }
        }
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

    g_sh.interactive = force_interactive || vs_plat_interactive();
    parser_init(&p, g_sh.interactive ? interactive_next_line : stdin_next_line,
                NULL);
    status = run_source(&p, g_sh.interactive);
    parser_free(&p);

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
