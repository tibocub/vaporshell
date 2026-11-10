/*
 * jobs.c -- background jobs (`cmd &`), and `jobs`, `wait`, `fg`, `bg`,
 * `disown`, `kill`'s job-spec support.
 *
 * A job here is one pid: a backgrounded pipeline is one forked child that
 * manages its own internal pipeline (exec.c). On POSIX the interactive shell
 * also has real terminal job control (see "Terminal job control" below); on
 * NuttX there is none, and a job is either running in the background or
 * finished.
 *
 * Job ids count up forever and are never reused, unlike bash, which reuses
 * a ended job's number once it is no longer displayed. This is simpler and
 * only matters to a script that depends on specific reused numbers, which
 * is rare enough to be an accepted difference (documented).
 */

#include <nuttx/config.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef VAPORSHELL_POSIX
#  include <fcntl.h>
#endif

#include "vaporshell.h"
#include "exec.h"
#include "mode.h"
#include "platform.h"

/* A reasonable stand-in for the source text bash would show in `jobs`: the
 * words of a simple command, unexpanded, joined by spaces; something short
 * and generic for anything else, since there is no general unparser here.
 */

static char *describe_node(const struct node_s *n)
{
  struct sbuf_s sb;
  const struct node_s *st;
  char *t;

  sb_init(&sb);
  if (n != NULL && n->type == N_PIPE)
    {
      for (st = n->a; st != NULL; st = st->next)
        {
          t = vash_node_text(st);
          sb_adds(&sb, sb.len > 0 ? " | " : "");
          sb_adds(&sb, t[0] != '\0' ? t : "(command)");
          free(t);
        }
    }
  else
    {
      t = vash_node_text(n);
      sb_adds(&sb, t[0] != '\0' ? t : "(command)");
      free(t);
    }

  return sb.s != NULL ? sb_take(&sb) : vash_xstrdup("");
}

static bool g_quiet_add;         /* a stopped foreground job is not announced as a new background one */

int job_add(pid_t pid, const char *cmd)
{
  struct job_s *j;

  if (g_sh.njobs == g_sh.jobs_cap)
    {
      g_sh.jobs_cap = g_sh.jobs_cap > 0 ? g_sh.jobs_cap * 2 : 8;
      g_sh.jobs = vash_xrealloc(g_sh.jobs, g_sh.jobs_cap * sizeof(*g_sh.jobs));
    }

  j = &g_sh.jobs[g_sh.njobs++];
  j->id = ++g_sh.next_job_id;
  j->pid = pid;
  j->cmd = vash_xstrdup(cmd != NULL ? cmd : "(command)");
  j->state = JOB_RUNNING;
  j->status = 0;
  j->notified = false;
  j->pgid = pid;

  g_sh.job_previous = g_sh.job_current;
  g_sh.job_current = j->id;
  if (g_sh.jc && !g_quiet_add)
    {
      fprintf(stderr, "[%d] %ld\n", j->id, (long)pid);     /* bash: `cmd &` tells the job number and pid */
    }

  return j->id;
}

/* cmd, freed here, is the node's description (see describe_node); callers
 * that have a node use job_add_node instead of building the text themselves.
 */

int job_add_node(pid_t pid, const struct node_s *n)
{
  char *cmd = describe_node(n);
  int id = job_add(pid, cmd);

  free(cmd);
  return id;
}

/* A WNOHANG (and, where the platform has it, WUNTRACED/WCONTINUED) poll of
 * every job still thought to be running or stopped; called before any of
 * `jobs`/`wait`/`fg`/`bg` do anything; only.
 */

void job_reap(void)
{
  size_t i;

  for (i = 0; i < g_sh.njobs; i++)
    {
      struct job_s *j = &g_sh.jobs[i];
      int wstatus;
      pid_t r;

      if (j->state == JOB_DONE)
        {
          continue;
        }

#ifdef WUNTRACED
      r = waitpid(j->pid, &wstatus, WNOHANG | WUNTRACED
#ifdef WCONTINUED
                  | WCONTINUED
#endif
                 );
#else
      r = waitpid(j->pid, &wstatus, WNOHANG);
#endif
      if (r <= 0)
        {
          continue;
        }

#ifdef WIFSTOPPED
      if (WIFSTOPPED(wstatus))
        {
          j->state = JOB_STOPPED;
          j->notified = false;
          continue;
        }
#endif

#ifdef WIFCONTINUED
      if (WIFCONTINUED(wstatus))
        {
          j->state = JOB_RUNNING;
          j->notified = false;
          continue;
        }
#endif

      j->state = JOB_DONE;
      j->notified = false;
      j->status = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 128 + WTERMSIG(wstatus);
    }
}

struct job_s *job_find_id(int id)
{
  size_t i;

  for (i = 0; i < g_sh.njobs; i++)
    {
      if (g_sh.jobs[i].id == id)
        {
          return &g_sh.jobs[i];
        }
    }

  return NULL;
}

/* %N, %%/%+ (current), %- (previous), a bare N meaning job N (as `kill`,
 * `wait` and `fg`/`bg` all accept), or a bare pid matching some job's
 * process. NULL (no spec) also means the current job. *bad_spec is set if
 * 'spec' looked like a job spec but named nothing that exists.
 */

struct job_s *job_find_spec(const char *spec, bool *bad_spec)
{
  struct job_s *j;

  *bad_spec = false;
  if (spec == NULL || spec[0] == '\0')
    {
      j = g_sh.job_current != 0 ? job_find_id(g_sh.job_current) : NULL;
      *bad_spec = j == NULL;
      return j;
    }

  if (spec[0] == '%')
    {
      const char *s = spec + 1;

      if (s[0] == '\0' || s[0] == '%' || s[0] == '+')
        {
          j = g_sh.job_current != 0 ? job_find_id(g_sh.job_current) : NULL;
        }
      else if (s[0] == '-')
        {
          j = g_sh.job_previous != 0 ? job_find_id(g_sh.job_previous) : NULL;
        }
      else if (s[0] >= '0' && s[0] <= '9')
        {
          j = job_find_id(atoi(s));
        }
      else
        {
          /* %name or %?name: a prefix, or substring, of some job's command */

          bool substr = s[0] == '?';
          const char *pat = substr ? s + 1 : s;
          size_t i;

          j = NULL;
          for (i = 0; i < g_sh.njobs; i++)
            {
              bool hit = substr ? strstr(g_sh.jobs[i].cmd, pat) != NULL
                                : strncmp(g_sh.jobs[i].cmd, pat, strlen(pat)) == 0;

              if (hit)
                {
                  j = &g_sh.jobs[i];
                }
            }
        }

      *bad_spec = j == NULL;
      return j;
    }

  {
    /* a bare pid, as `wait PID` and `kill PID` both still accept */

    pid_t pid = (pid_t)atol(spec);
    size_t i;

    for (i = 0; i < g_sh.njobs; i++)
      {
        if (g_sh.jobs[i].pid == pid)
          {
            return &g_sh.jobs[i];
          }
      }
  }

  *bad_spec = true;
  return NULL;
}

void job_remove(struct job_s *j)
{
  size_t idx = (size_t)(j - g_sh.jobs);

  free(j->cmd);
  if (g_sh.job_current == j->id)
    {
      g_sh.job_current = g_sh.job_previous;
      g_sh.job_previous = 0;
    }
  else if (g_sh.job_previous == j->id)
    {
      g_sh.job_previous = 0;
    }

  memmove(g_sh.jobs + idx, g_sh.jobs + idx + 1,
          (g_sh.njobs - idx - 1) * sizeof(*g_sh.jobs));
  g_sh.njobs--;
}

/* ---- Display -------------------------------------------------------------------- */

static const char *state_text(const struct job_s *j, char *buf, size_t bufsz)
{
  const char *sig;

  switch (j->state)
    {
      case JOB_STOPPED:
        return "Stopped";

      case JOB_RUNNING:
        return "Running";

      default:
        if (j->status == 0)
          {
            return "Done";
          }

        if (j->status > 128)
          {
            sig = strsignal(j->status - 128);
            if (sig != NULL)
              {
                return sig;
              }

            snprintf(buf, bufsz, "Signal %d", j->status - 128);
            return buf;
          }

        snprintf(buf, bufsz, "Exit %d", j->status);
        return buf;
    }
}

static void print_job_line(const struct job_s *j, bool pid_only, bool long_form)
{
  char mark = j->id == g_sh.job_current ? '+' : j->id == g_sh.job_previous ? '-' : ' ';
  char buf[32];

  if (pid_only)
    {
      printf("%ld\n", (long)j->pid);
      return;
    }

  printf("[%d]%c ", j->id, mark);
  if (long_form)
    {
      printf("%5ld ", (long)j->pid);
    }
  else
    {
      putchar(' ');                /* the plain format's extra column, that -l skips */
    }

  printf("%-27s%s", state_text(j, buf, sizeof(buf)), j->cmd);
  if (j->state == JOB_RUNNING)
    {
      fputs(" &", stdout);
    }

  putchar('\n');
}

int bi_jobs(int argc, char **argv)
{
  bool pid_only = false;
  bool long_form = false;
  bool changed_only = false;
  int i;
  size_t k;

  job_reap();
  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "-p") == 0)
        {
          pid_only = true;
        }
      else if (strcmp(argv[i], "-l") == 0)
        {
          long_form = true;
        }
      else if (strcmp(argv[i], "-n") == 0)
        {
          changed_only = true;
        }
      else if (strcmp(argv[i], "--") != 0 && vash_feat(VF_BASH_SYNTAX))
        {
          vash_err("jobs: %s: invalid option", argv[i]);
          return 2;
        }
    }

  for (k = 0; k < g_sh.njobs; k++)
    {
      struct job_s *j = &g_sh.jobs[k];

      if (changed_only && j->notified)
        {
          continue;
        }

      print_job_line(j, pid_only, long_form);
      j->notified = true;
    }

  /* a job shown as Done has been reported; bash drops it from the table now */

  for (k = 0; k < g_sh.njobs; )
    {
      if (g_sh.jobs[k].state == JOB_DONE)
        {
          job_remove(&g_sh.jobs[k]);
        }
      else
        {
          k++;
        }
    }

  return 0;
}


/* ---- Terminal job control (POSIX only) -------------------------------------------
 *
 * The interactive shell leads its own process group and owns the terminal;
 * every foreground job (a command, a pipeline, a ( ) group) and every `&`
 * job gets a group of its own. A foreground job is handed the terminal and
 * waited for with WUNTRACED, so ^Z stops it and the shell takes the terminal
 * back, keeping it as a stopped job for fg/bg. Forked children (platform.c)
 * reset ^Z/TTIN/TTOU, which the shell itself ignores.
 *
 * A job is still one pid: for a stopped pipeline, the member that was seen
 * stopping; the others are stopped by the same signal and resumed by the
 * group SIGCONT, but nothing reaps them (they are reaped when the shell
 * exits).
 */

#ifdef VAPORSHELL_POSIX

void jc_init(void)
{
  int fd;

  if (!g_sh.interactive || !isatty(STDIN_FILENO) || g_sh.in_subshell > 0)
    {
      return;
    }

  fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
  if (fd < 0)
    {
      return;
    }

  g_sh.jc_tty = vash_plat_dup_high(fd);
  close(fd);
  if (g_sh.jc_tty < 0)
    {
      return;
    }

  signal(SIGTSTP, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
  setpgid(0, 0);                       /* fails harmlessly if we already lead one */
  g_sh.jc_shell_pgrp = getpgrp();
  tcsetpgrp(g_sh.jc_tty, g_sh.jc_shell_pgrp);
  g_sh.jc = true;
  vash_set_named_option("monitor", true);        /* set -m: what bash shows once it has job control */
}

void jc_begin(const struct node_s *n)
{
  g_sh.jc_pgid = 0;
  g_sh.jc_stopped = false;
  g_sh.jc_node = n;
  g_sh.jc_want = g_sh.jc;
}

void jc_launched(bool fg)
{
  if (!g_sh.jc_want)
    {
      return;
    }

  g_sh.jc_want = false;
  if (fg && g_sh.jc_pgid > 0)
    {
      tcsetpgrp(g_sh.jc_tty, g_sh.jc_pgid);
      kill(-g_sh.jc_pgid, SIGCONT);    /* a child that read the tty before this was stopped by SIGTTIN */
    }
}

static void jc_back(void)
{
  tcsetpgrp(g_sh.jc_tty, g_sh.jc_shell_pgrp);
}

void jc_wait_done(void)
{
  if (g_sh.jc)
    {
      jc_back();
    }
}

static int jc_stop_notice(pid_t pid, pid_t pgid, const struct node_s *n, int sig)
{
  int id;
  struct job_s *j;

  g_quiet_add = true;
  id = job_add_node(pid, n != NULL ? n : g_sh.cur_node);
  g_quiet_add = false;
  j = job_find_id(id);

  j->state = JOB_STOPPED;
  j->pgid = pgid;
  j->notified = true;
  fputc('\n', stdout);
  print_job_line(j, false, false);
  fflush(stdout);
  g_sh.jc_stopped = true;
  return 128 + sig;
}

int jc_wait(pid_t pid, bool last)
{
  int wstatus;

  if (!g_sh.jc)
    {
      return -1;
    }

  if (g_sh.jc_stopped)
    {
      return 148;
    }

  while (waitpid(pid, &wstatus, WUNTRACED) < 0)
    {
      if (errno != EINTR)
        {
          jc_back();
          return 1;
        }
    }

  if (WIFSTOPPED(wstatus))
    {
      int st = jc_stop_notice(pid, g_sh.jc_pgid, g_sh.jc_node, WSTOPSIG(wstatus));

      jc_back();
      return st;
    }

  if (last)
    {
      jc_back();
    }

  return WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 128 + WTERMSIG(wstatus);
}

#else

void jc_wait_done(void) {}
void jc_init(void) {}
void jc_begin(const struct node_s *n) { (void)n; }
void jc_launched(bool fg) { (void)fg; }
int jc_wait(pid_t pid, bool last) { (void)pid; (void)last; return -1; }

#endif

/* ---- fg / bg / disown ------------------------------------------------------------ */

int bi_fg(int argc, char **argv)
{
  bool bad;
  struct job_s *j;

  job_reap();
  j = job_find_spec(argc > 1 ? argv[1] : NULL, &bad);
  if (j == NULL)
    {
      vash_err("fg: %s: no such job", argc > 1 ? argv[1] : "current");
      return 1;
    }

#ifdef VAPORSHELL_POSIX
  if (g_sh.jc && j->state != JOB_DONE)
    {
      tcsetpgrp(g_sh.jc_tty, j->pgid);
    }
#endif

#ifdef SIGCONT
  if (j->state == JOB_STOPPED)
    {
      kill(g_sh.jc ? -j->pgid : j->pid, SIGCONT);
      j->state = JOB_RUNNING;
    }
#endif

  printf("%s\n", j->cmd);
  fflush(stdout);
  while (j->state == JOB_RUNNING)
    {
      int wstatus;
      pid_t r = waitpid(j->pid, &wstatus, g_sh.jc ? WUNTRACED : 0);

      if (r < 0)
        {
          break;
        }

#ifdef WIFSTOPPED
      if (WIFSTOPPED(wstatus))
        {
          j->state = JOB_STOPPED;
          j->notified = true;
          fputc('\n', stdout);
          print_job_line(j, false, false);
          jc_wait_done();
          return 128 + WSTOPSIG(wstatus);
        }
#endif

      j->state = JOB_DONE;
      j->status = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 128 + WTERMSIG(wstatus);
    }

  jc_wait_done();

  {
    int status = j->status;

    job_remove(j);
    return status;
  }
}

int bi_bg(int argc, char **argv)
{
  bool bad;
  struct job_s *j;

  job_reap();
  j = job_find_spec(argc > 1 ? argv[1] : NULL, &bad);
  if (j == NULL)
    {
      vash_err("bg: %s: no such job", argc > 1 ? argv[1] : "current");
      return 1;
    }

  if (j->state != JOB_STOPPED)
    {
      vash_err("bg: job %d already in background", j->id);
      return 1;
    }

#ifdef SIGCONT
  kill(g_sh.jc ? -j->pgid : j->pid, SIGCONT);
#endif
  j->state = JOB_RUNNING;
  printf("[%d]%c %s &\n", j->id, j->id == g_sh.job_current ? '+' : '-', j->cmd);
  return 0;
}

int bi_disown(int argc, char **argv)
{
  int i;
  bool all = false;

  job_reap();
  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "-a") == 0)
        {
          all = true;
        }
      else if (strcmp(argv[i], "-r") == 0)
        {
          /* only running jobs: already the common case here (no real stop) */
        }
    }

  if (all || argc == 1)
    {
      size_t k;

      for (k = 0; k < g_sh.njobs; k++)
        {
          free(g_sh.jobs[k].cmd);
        }

      g_sh.njobs = 0;
      g_sh.job_current = 0;
      g_sh.job_previous = 0;
      return 0;
    }

  for (i = 1; i < argc; i++)
    {
      bool bad;
      struct job_s *j = job_find_spec(argv[i], &bad);

      if (j == NULL)
        {
          vash_err("disown: %s: no such job", argv[i]);
          return 1;
        }

      job_remove(j);
    }

  return 0;
}

/* fn(spec, command, arg) for every job, spec being "%N". For completion. */

void vash_each_job(void (*fn)(const char *spec, const char *cmd, void *arg), void *arg)
{
  size_t i;

  for (i = 0; i < g_sh.njobs; i++)
    {
      char spec[16];

      snprintf(spec, sizeof(spec), "%%%d", g_sh.jobs[i].id);
      fn(spec, g_sh.jobs[i].cmd != NULL ? g_sh.jobs[i].cmd : "", arg);
    }
}
