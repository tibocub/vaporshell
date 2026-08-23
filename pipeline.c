/*
 * pipeline.c -- runs a sequence of commands connected by pipes
 * (`cmd1 | cmd2 | cmd3`), distinct from '||': by the time a segment's
 * text ever reaches this file, line.c's own split_line() has already
 * consumed any '||' as a segment boundary, so a bare, single '|'
 * found here is unambiguous.
 *
 * Requires CONFIG_SCHED_CHILD_STATUS=y. Confirmed directly, the hard
 * way: without it, waitpid() can only retrieve a child's exit status
 * if the child hasn't already exited by the time waitpid() is called
 * on it (NuttX's own Kconfig help text for this option describes
 * exactly this race). Spawning every stage first and only waiting
 * afterward -- necessary here, since all stages have to be running
 * concurrently for data to actually flow through the pipes between
 * them -- means a fast-exiting stage (e.g. `true`) routinely beats
 * this function's own wait loop to the punch. Without this option,
 * that waitpid() call fails with ECHILD, and *since that failure was
 * originally left unchecked*, the uninitialized status variable
 * happened to still hold the previous stage's own value on the
 * stack -- silently reporting the wrong stage's exit status instead
 * of erroring. Single-command execution (run_command() in exec.c)
 * never hit this, since it waits immediately after spawning, before
 * a child has any realistic chance to exit first.
 *
 * Deliberately spawned-commands only for now: builtins (cd, ., etc.)
 * as a pipeline stage aren't supported. A builtin runs in *this*
 * process, not a spawned one -- giving it a redirected stdin/stdout
 * would mean actually redirecting this shell's own fds temporarily
 * and restoring them afterward, a real, separate piece of work, not
 * something spawn_command()'s own file_actions mechanism can do for
 * something that never spawns at all. If a builtin name shows up as a
 * pipeline stage here, it just fails to spawn (ENOENT), the same as
 * any nonexistent program would.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <errno.h>

#include "vaporshell.h"
#include "expand.h"

#define MAX_STAGES 16

struct stage_s
{
    FAR char *text;
};

/****************************************************************************
 * True iff 'text' contains an unquoted, single '|' -- checked before
 * line.c decides whether to treat a segment as a pipeline at all, so
 * the common, no-pipe case behaves exactly as it always has.
 ****************************************************************************/

bool has_unquoted_pipe(FAR const char *text)
{
    char quote = '\0';

    while (*text != '\0')
    {
        if (quote != '\0')
        {
            if (*text == quote)
            {
                quote = '\0';
            }

            text++;
            continue;
        }

        if (*text == '\'' || *text == '"')
        {
            quote = *text;
            text++;
            continue;
        }

        if (*text == '|' && text[1] == '|')
        {
            text += 2;
            continue;
        }

        if (*text == '|')
        {
            return true;
        }

        text++;
    }

    return false;
}

/****************************************************************************
 * Splits 'text' (destructively, like tokenize()/split_line() --
 * stages[].text point into 'text's own buffer) on unquoted, single
 * '|'. '||' pairs are skipped over rather than split on, defensively
 * -- in practice a segment's text shouldn't contain one at all by the
 * time it reaches here, since line.c's own split_line() already
 * consumes '||' as a segment boundary first.
 ****************************************************************************/

static int split_pipeline(FAR char *text, struct stage_s stages[],
                           int max_stages)
{
    int count = 0;
    FAR char *p = text;
    FAR char *start = text;
    char quote = '\0';

    while (*p != '\0')
    {
        if (quote != '\0')
        {
            if (*p == quote)
            {
                quote = '\0';
            }

            p++;
            continue;
        }

        if (*p == '\'' || *p == '"')
        {
            quote = *p;
            p++;
            continue;
        }

        if (*p == '|' && p[1] == '|')
        {
            p += 2;
            continue;
        }

        if (*p == '|')
        {
            if (count < max_stages - 1)
            {
                *p = '\0';
                stages[count++].text = start;
            }

            p++;
            start = p;
            continue;
        }

        p++;
    }

    if (count < max_stages - 1)
    {
        stages[count++].text = start;
    }

    return count;
}

/****************************************************************************
 * Tokenizes and expands one stage's text into a newly-malloc()'d argv
 * (caller frees each entry). Shared by every stage below -- one place
 * for "how does a single pipeline stage turn into a spawnable argv".
 ****************************************************************************/

static int build_stage_argv(FAR char *text, FAR char *argv_out[])
{
    FAR char *raw_tokens[MAX_TOKENS];
    bool no_expand[MAX_TOKENS];
    int ntok;

    ntok = tokenize(text, raw_tokens, no_expand, MAX_TOKENS);
    expand_tokens(raw_tokens, no_expand, ntok, argv_out);

    return ntok;
}

int run_pipeline(FAR char *text)
{
    struct stage_s stages[MAX_STAGES];
    int nstages;
    int pipefds[MAX_STAGES - 1][2];
    pid_t pids[MAX_STAGES];
    int last_status = 1;
    int i;

    nstages = split_pipeline(text, stages, MAX_STAGES);

    if (nstages < 2)
    {
        /* Shouldn't happen -- line.c only calls this function after
         * has_unquoted_pipe() already confirmed a real pipe exists --
         * but fall back to running it as a single, ordinary command
         * rather than assume that can never happen.
         */

        FAR char *argv[MAX_TOKENS];
        int ntok;
        int status;
        int j;

        if (nstages == 0)
        {
            return 0;
        }

        ntok = build_stage_argv(stages[0].text, argv);
        status = run_command(ntok, argv);

        for (j = 0; j < ntok; j++)
        {
            free(argv[j]);
        }

        return status;
    }

    for (i = 0; i < nstages - 1; i++)
    {
        if (pipe(pipefds[i]) != 0)
        {
            fprintf(stderr, "vaporshell: pipe: %s\n", strerror(errno));

            while (--i >= 0)
            {
                close(pipefds[i][0]);
                close(pipefds[i][1]);
            }

            return 1;
        }
    }

    for (i = 0; i < nstages; i++)
    {
        FAR char *argv[MAX_TOKENS];
        int ntok;
        int j;
        posix_spawn_file_actions_t actions;
        int ret;
        int k;

        ntok = build_stage_argv(stages[i].text, argv);

        if (ntok == 0)
        {
            pids[i] = -1;
            for (j = 0; j < ntok; j++)
            {
                free(argv[j]);
            }

            continue;
        }

        posix_spawn_file_actions_init(&actions);

        if (i > 0)
        {
            posix_spawn_file_actions_adddup2(&actions, pipefds[i - 1][0],
                                              STDIN_FILENO);
        }

        if (i < nstages - 1)
        {
            posix_spawn_file_actions_adddup2(&actions, pipefds[i][1],
                                              STDOUT_FILENO);
        }

        /* Every stage needs every pipe's *original* fd numbers closed
         * too, once dup2 (above) has already put the ones it actually
         * needs onto stdin/stdout -- otherwise each child inherits
         * every pipe end this shell itself opened, and a reader stage
         * never sees EOF, because it's still holding its own
         * redundant copy of some writer's write end open even after
         * that writer has exited and closed its own.
         */

        for (k = 0; k < nstages - 1; k++)
        {
            posix_spawn_file_actions_addclose(&actions, pipefds[k][0]);
            posix_spawn_file_actions_addclose(&actions, pipefds[k][1]);
        }

        ret = spawn_command(ntok, argv, &actions, &pids[i]);

        posix_spawn_file_actions_destroy(&actions);

        if (ret != 0)
        {
            fprintf(stderr, "vaporshell: %s: %s\n", argv[0], strerror(ret));
            pids[i] = -1;
        }

        for (j = 0; j < ntok; j++)
        {
            free(argv[j]);
        }
    }

    /* This shell's own copies of every pipe fd have to close too, or
     * the same EOF problem hits from the parent's side: as long as
     * *any* process (including this one) still holds a pipe's write
     * end open, its reader waits forever for a EOF that never comes.
     */

    for (i = 0; i < nstages - 1; i++)
    {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }

    for (i = 0; i < nstages; i++)
    {
        int status;
        int wret;

        if (pids[i] < 0)
        {
            continue;
        }

        wret = waitpid(pids[i], &status, 0);

        if (i == nstages - 1)
        {
            /* wret < 0 (see this file's own top-of-file comment on
             * CONFIG_SCHED_CHILD_STATUS): status was never actually
             * written by waitpid() in that case, so it must not be
             * read here either -- report a real, honest failure
             * instead of an uninitialized value that happens to look
             * like a plausible exit code.
             */

            if (wret < 0)
            {
                fprintf(stderr, "vaporshell: waitpid: %s\n", strerror(errno));
                last_status = 1;
            }
            else
            {
                last_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
            }
        }
    }

    return last_status;
}
