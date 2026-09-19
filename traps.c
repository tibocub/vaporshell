/*
 * traps.c -- `trap`, `kill` and the machinery behind them.
 *
 * A signal handler only sets a flag; the executor calls
 * trap_run_pending() between commands, where running shell code is safe.
 * The EXIT trap (number 0) exists on every platform; real signals are
 * only wired up on the standalone build for now.
 */

#include <nuttx/config.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vaporshell.h"
#include "exec.h"

#define NTRAPS 65

static char *g_action[NTRAPS];     /* NULL: default, "": ignore */
static volatile int g_sig_flag[NTRAPS];
volatile int g_trap_pending;

#ifdef VAPORSHELL_POSIX

static const struct
{
  const char *name;
  int num;
} g_sigs[] =
{
  { "EXIT", 0 },
  { "HUP", SIGHUP }, { "INT", SIGINT }, { "QUIT", SIGQUIT },
  { "ILL", SIGILL }, { "ABRT", SIGABRT }, { "FPE", SIGFPE },
  { "KILL", SIGKILL }, { "SEGV", SIGSEGV }, { "PIPE", SIGPIPE },
  { "ALRM", SIGALRM }, { "TERM", SIGTERM }, { "USR1", SIGUSR1 },
  { "USR2", SIGUSR2 }, { "CHLD", SIGCHLD }, { "CONT", SIGCONT },
  { "STOP", SIGSTOP }, { "TSTP", SIGTSTP }, { "TTIN", SIGTTIN },
  { "TTOU", SIGTTOU }
};

#else

static const struct
{
  const char *name;
  int num;
} g_sigs[] =
{
  { "EXIT", 0 }
};

#endif

#define NSIGS ((int)(sizeof(g_sigs) / sizeof(g_sigs[0])))

/* Accepts a number, NAME or SIGNAME; returns -1 if unknown. */

static int sig_number(const char *s)
{
  int i;

  if (s[0] >= '0' && s[0] <= '9')
    {
      char *end;
      long v = strtol(s, &end, 10);

      return (*end == '\0' && v >= 0 && v < NTRAPS) ? (int)v : -1;
    }

  if (strncmp(s, "SIG", 3) == 0)
    {
      s += 3;
    }

  for (i = 0; i < NSIGS; i++)
    {
      if (strcmp(s, g_sigs[i].name) == 0)
        {
          return g_sigs[i].num;
        }
    }

  return -1;
}

static const char *sig_name(int num)
{
  int i;

  for (i = 0; i < NSIGS; i++)
    {
      if (g_sigs[i].num == num)
        {
          return g_sigs[i].name;
        }
    }

  return "?";
}

#ifdef VAPORSHELL_POSIX

static void on_signal(int sig)
{
  if (sig > 0 && sig < NTRAPS)
    {
      g_sig_flag[sig] = 1;
      g_trap_pending = 1;
    }
}

static void install(int sig, const char *action)
{
  struct sigaction sa;

  if (sig == 0)
    {
      return;
    }

  memset(&sa, 0, sizeof(sa));
  sigemptyset(&sa.sa_mask);
  if (action == NULL)
    {
      sa.sa_handler = SIG_DFL;
    }
  else if (action[0] == '\0')
    {
      sa.sa_handler = SIG_IGN;
    }
  else
    {
      sa.sa_handler = on_signal;
    }

  sigaction(sig, &sa, NULL);
}

#else

static void install(int sig, const char *action)
{
  (void)sig;
  (void)action;
}

#endif

static void set_trap(int sig, const char *action)
{
  free(g_action[sig]);
  g_action[sig] = action != NULL ? vs_xstrdup(action) : NULL;
  install(sig, g_action[sig]);
}

static void print_traps(void)
{
  int i;

  for (i = 0; i < NTRAPS; i++)
    {
      if (g_action[i] != NULL)
        {
          const char *p;

          fputs("trap -- '", stdout);
          for (p = g_action[i]; *p != '\0'; p++)
            {
              if (*p == '\'')
                {
                  fputs("'\\''", stdout);
                }
              else
                {
                  putchar(*p);
                }
            }

          printf("' %s\n", sig_name(i));
        }
    }
}

int bi_trap(int argc, char **argv)
{
  int i = 1;
  const char *action;
  int status = 0;

  if (i < argc && strcmp(argv[i], "--") == 0)
    {
      i++;
    }

  if (i < argc && strcmp(argv[i], "-p") == 0)
    {
      print_traps();
      return 0;
    }

  if (i >= argc)
    {
      print_traps();
      return 0;
    }

  /* A first operand that is an unsigned number means "reset these". */

  if (argv[i][0] >= '0' && argv[i][0] <= '9')
    {
      action = NULL;
    }
  else
    {
      action = argv[i++];
      if (strcmp(action, "-") == 0)
        {
          action = NULL;
        }
    }

  if (i >= argc)
    {
      vs_err("trap: usage: trap [action] condition...");
      return 2;
    }

  for (; i < argc; i++)
    {
      int sig = sig_number(argv[i]);

      if (sig < 0)
        {
          vs_err("trap: %s: invalid signal specification", argv[i]);
          status = 1;
        }
#ifdef VAPORSHELL_POSIX
      else if (sig == SIGKILL || sig == SIGSTOP)
        {
          vs_err("trap: %s: cannot be trapped", argv[i]);
          status = 1;
        }
#endif
      else
        {
          set_trap(sig, action);
        }
    }

  return status;
}

/* Runs an action with $? preserved across it. */

static void run_action(const char *action)
{
  int saved = g_sh.last_status;
  enum unwind_e uw = g_sh.unwind;
  int count = g_sh.unwind_count;

  g_sh.unwind = UW_NONE;
  run_string(action, strlen(action));
  if (g_sh.unwind == UW_NONE)
    {
      g_sh.unwind = uw;
      g_sh.unwind_count = count;
      g_sh.last_status = saved;
    }
}

void trap_run_pending(void)
{
  int i;

  g_trap_pending = 0;
  for (i = 1; i < NTRAPS; i++)
    {
      if (g_sig_flag[i])
        {
          g_sig_flag[i] = 0;
          if (g_action[i] != NULL && g_action[i][0] != '\0')
            {
              char *copy = vs_xstrdup(g_action[i]);

              run_action(copy);
              free(copy);
            }
        }
    }
}

void trap_run_exit(void)
{
  char *action = g_action[0];

  if (action != NULL && action[0] != '\0')
    {
      g_action[0] = NULL;        /* run once */
      run_action(action);
      free(action);
    }
}

/* A forked subshell keeps ignored signals ignored and drops the rest. */

void trap_reset_in_child(void)
{
  int i;

  for (i = 0; i < NTRAPS; i++)
    {
      if (g_action[i] != NULL && g_action[i][0] != '\0')
        {
          free(g_action[i]);
          g_action[i] = NULL;
          install(i, NULL);
        }
    }

  memset((void *)g_sig_flag, 0, sizeof(g_sig_flag));
  g_trap_pending = 0;
}

int bi_kill(int argc, char **argv)
{
#ifdef VAPORSHELL_POSIX
  int sig = SIGTERM;
  int i = 1;
  int status = 0;

  if (i < argc && argv[i][0] == '-' && argv[i][1] != '\0' &&
      strcmp(argv[i], "--") != 0)
    {
      if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
        {
          sig = sig_number(argv[++i]);
        }
      else
        {
          sig = sig_number(argv[i] + 1);
        }

      if (sig < 0)
        {
          vs_err("kill: invalid signal specification");
          return 2;
        }

      i++;
    }

  if (i < argc && strcmp(argv[i], "--") == 0)
    {
      i++;
    }

  if (i >= argc)
    {
      vs_err("kill: usage: kill [-s sig | -sig] pid...");
      return 2;
    }

  for (; i < argc; i++)
    {
      if (kill((pid_t)atol(argv[i]), sig) != 0)
        {
          vs_err("kill: %s: %s", argv[i], strerror(errno));
          status = 1;
        }
    }

  return status;
#else
  (void)argc;
  (void)argv;
  vs_err("kill: not supported on this platform yet");
  return 1;
#endif
}
