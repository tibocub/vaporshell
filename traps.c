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
#include "mode.h"
#include "exec.h"

#define NTRAPS VS_NTRAPS

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
  { "TTOU", SIGTTOU },
#ifdef SIGTRAP
  { "TRAP", SIGTRAP },
#endif
#ifdef SIGBUS
  { "BUS", SIGBUS },
#endif
#ifdef SIGURG
  { "URG", SIGURG },
#endif
#ifdef SIGSYS
  { "SYS", SIGSYS },
#endif
#ifdef SIGVTALRM
  { "VTALRM", SIGVTALRM },
#endif
#ifdef SIGPROF
  { "PROF", SIGPROF },
#endif
#ifdef SIGXCPU
  { "XCPU", SIGXCPU },
#endif
#ifdef SIGXFSZ
  { "XFSZ", SIGXFSZ },
#endif
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
      g_sh.trap_flag[sig] = 1;
      g_sh.trap_pending = 1;
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
  free(g_sh.trap_action[sig]);
  g_sh.trap_action[sig] = action != NULL ? vs_xstrdup(action) : NULL;
  install(sig, g_sh.trap_action[sig]);
}

static void print_traps(void)
{
  int i;

  for (i = 0; i < NTRAPS; i++)
    {
      if (g_sh.trap_action[i] != NULL)
        {
          const char *p;

          fputs("trap -- '", stdout);
          for (p = g_sh.trap_action[i]; *p != '\0'; p++)
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
  g_sh.trap_depth++;
  run_string(action, strlen(action));
  g_sh.trap_depth--;
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

  g_sh.trap_pending = 0;
  for (i = 1; i < NTRAPS; i++)
    {
      if (g_sh.trap_flag[i])
        {
          g_sh.trap_flag[i] = 0;
          if (g_sh.trap_action[i] != NULL && g_sh.trap_action[i][0] != '\0')
            {
              char *copy = vs_xstrdup(g_sh.trap_action[i]);

              run_action(copy);
              free(copy);
            }
        }
    }
}

void trap_run_exit(void)
{
  char *action = g_sh.trap_action[0];

  if (action != NULL && action[0] != '\0')
    {
      g_sh.trap_action[0] = NULL;        /* run once */
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
      if (g_sh.trap_action[i] != NULL && g_sh.trap_action[i][0] != '\0')
        {
          free(g_sh.trap_action[i]);
          g_sh.trap_action[i] = NULL;
          install(i, NULL);
        }
    }

  memset((void *)g_sh.trap_flag, 0, sizeof(g_sh.trap_flag));
  g_sh.trap_pending = 0;
}

#ifdef VAPORSHELL_POSIX

static int cmp_sig(const void *a, const void *b)
{
  int x = *(const int *)a;
  int y = *(const int *)b;

  return x - y;
}

/* kill -l [number]: the names we know, by signal number. dash prints them
 * one per line after a leading 0; bash in rows of five as `NN) SIGNAME`.
 */

static int kill_list(int argc, char **argv)
{
  int order[NSIGS];
  int n = 0;
  int k;

  if (argc > 2)
    {
      int num = atoi(argv[2]);

      for (k = 1; k < NSIGS; k++)
        {
          if (g_sigs[k].num == num)
            {
              puts(g_sigs[k].name);
              return 0;
            }
        }

      vs_err("kill: %s: invalid signal specification", argv[2]);
      return 1;
    }

  for (k = 1; k < NSIGS; k++)
    {
      order[n++] = g_sigs[k].num;
    }

  qsort(order, (size_t)n, sizeof(int), cmp_sig);
  if (!vs_feat(VF_BASH_INFO_FORMATS))
    {
      puts("0");
    }

  for (k = 0; k < n; k++)
    {
      int j;

      for (j = 1; j < NSIGS; j++)
        {
          if (g_sigs[j].num == order[k])
            {
              break;
            }
        }

      if (vs_feat(VF_BASH_INFO_FORMATS))
        {
          printf("%2d) SIG%s%s", order[k], g_sigs[j].name,
                 (k % 5 == 4 || k == n - 1) ? "\n" : "\t");
        }
      else
        {
          puts(g_sigs[j].name);
        }
    }

  return 0;
}

#endif /* VAPORSHELL_POSIX */

int bi_kill(int argc, char **argv)
{
#ifdef VAPORSHELL_POSIX
  int sig = SIGTERM;
  int i = 1;
  int status = 0;

  if (argc > 1 && strcmp(argv[1], "-l") == 0)
    {
      return kill_list(argc, argv);
    }

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
