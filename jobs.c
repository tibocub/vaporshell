/*
 * jobs.c -- background jobs (`cmd &`), and `jobs`, `wait`, `fg`, `bg`,
 * `disown`, `kill`'s job-spec support.
 *
 * A job here is one pid: a backgrounded pipeline is one forked child that
 * manages its own internal pipeline (exec.c), so there is never more than
 * one process to track per job. There is no real terminal job control
 * (process groups, tcsetpgrp, SIGTSTP) -- `fg`/`bg` work with what a job
 * can actually be here: running in the background, or finished. A job this
 * shell did not itself stop can still be observed as JOB_STOPPED if it
 * receives SIGSTOP from elsewhere, but `bg` is the only way to resume it,
 * since nothing here suspends a job in the first place.
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

#include "vaporshell.h"
#include "exec.h"
#include "mode.h"

/* A reasonable stand-in for the source text bash would show in `jobs`: the
 * words of a simple command, unexpanded, joined by spaces; something short
 * and generic for anything else, since there is no general unparser here.
 */

static char *describe_node(const struct node_s *n)
{
  struct sbuf_s sb;
  const struct word_s *w;

  sb_init(&sb);
  if (n != NULL && n->type == N_SIMPLE)
    {
      for (w = n->words; w != NULL; w = w->next)
        {
          if (sb.len > 0)
            {
              sb_addc(&sb, ' ');
            }

          sb_adds(&sb, w->text);
        }
    }
  else if (n != NULL && n->type == N_PIPE && n->a != NULL)
    {
      for (w = n->a->words; w != NULL; w = w->next)
        {
          if (sb.len > 0)
            {
              sb_addc(&sb, ' ');
            }

          sb_adds(&sb, w->text);
        }

      sb_adds(&sb, " | ...");
    }
  else
    {
      sb_adds(&sb, "(command)");
    }

  return sb.s != NULL ? sb_take(&sb) : vs_xstrdup("");
}

int job_add(pid_t pid, const char *cmd)
{
  struct job_s *j;

  if (g_sh.njobs == g_sh.jobs_cap)
    {
      g_sh.jobs_cap = g_sh.jobs_cap > 0 ? g_sh.jobs_cap * 2 : 8;
      g_sh.jobs = vs_xrealloc(g_sh.jobs, g_sh.jobs_cap * sizeof(*g_sh.jobs));
    }

  j = &g_sh.jobs[g_sh.njobs++];
  j->id = ++g_sh.next_job_id;
  j->pid = pid;
  j->cmd = vs_xstrdup(cmd != NULL ? cmd : "(command)");
  j->state = JOB_RUNNING;
  j->status = 0;
  j->notified = false;

  g_sh.job_previous = g_sh.job_current;
  g_sh.job_current = j->id;
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
      else if (strcmp(argv[i], "--") != 0 && vs_feat(VF_BASH_SYNTAX))
        {
          vs_err("jobs: %s: invalid option", argv[i]);
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

/* ---- fg / bg / disown ------------------------------------------------------------ */

int bi_fg(int argc, char **argv)
{
  bool bad;
  struct job_s *j;

  job_reap();
  j = job_find_spec(argc > 1 ? argv[1] : NULL, &bad);
  if (j == NULL)
    {
      vs_err("fg: %s: no such job", argc > 1 ? argv[1] : "current");
      return 1;
    }

#ifdef SIGCONT
  if (j->state == JOB_STOPPED)
    {
      kill(j->pid, SIGCONT);
      j->state = JOB_RUNNING;
    }
#endif

  printf("%s\n", j->cmd);
  while (j->state == JOB_RUNNING)
    {
      int wstatus;
      pid_t r = waitpid(j->pid, &wstatus, 0);

      if (r < 0)
        {
          break;
        }

      j->state = JOB_DONE;
      j->status = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 128 + WTERMSIG(wstatus);
    }

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
      vs_err("bg: %s: no such job", argc > 1 ? argv[1] : "current");
      return 1;
    }

  if (j->state != JOB_STOPPED)
    {
      vs_err("bg: job %d already in background", j->id);
      return 1;
    }

#ifdef SIGCONT
  kill(j->pid, SIGCONT);
#endif
  j->state = JOB_RUNNING;
  printf("[%d]%c  %s &\n", j->id, j->id == g_sh.job_current ? '+' : '-', j->cmd);
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
          vs_err("disown: %s: no such job", argv[i]);
          return 1;
        }

      job_remove(j);
    }

  return 0;
}
