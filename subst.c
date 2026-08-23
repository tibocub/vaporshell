/*
 * subst.c -- command substitution's actual work: run a command,
 * capture its stdout. Deliberately implemented as a recursive
 * `vaporshell -c "cmd_text"` spawn, redirected into a pipe, rather
 * than trying to redirect run_command()'s own top-level output
 * in-process -- this reuses the *entire* existing pipeline (line.c's
 * splitting on ;/&&/||, expand.c's own variable expansion, and,
 * since this file is what expand.c itself calls into, nested command
 * substitution too) for free, instead of duplicating any of it here.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <errno.h>

#include "subst.h"

FAR char *capture_command_output(FAR const char *cmd_text)
{
    int pipefd[2];
    posix_spawn_file_actions_t actions;
    pid_t pid;
    FAR char *argv[4];
    int ret;
    int status;
    size_t cap = 256;
    size_t len = 0;
    FAR char *out;

    if (pipe(pipefd) != 0)
    {
        return strdup("");
    }

    if (posix_spawn_file_actions_init(&actions) != 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        return strdup("");
    }

    /* The child only ever writes, so it doesn't need the read end at
     * all -- closed first. Then its stdout *becomes* the pipe's write
     * end (dup2), and the original write-end fd (now a duplicate) is
     * closed too. All three of these are file actions applied by the
     * spawn implementation to the child, between its creation and
     * exec -- nothing here touches this process's own fds directly.
     */

    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);

    argv[0] = "vaporshell";
    argv[1] = "-c";
    argv[2] = (FAR char *)cmd_text;
    argv[3] = NULL;

    ret = posix_spawnp(&pid, "vaporshell", &actions, NULL, argv, environ);

    posix_spawn_file_actions_destroy(&actions);

    /* This process's own copy of the write end has to close too, or
     * the read() loop below blocks forever: as long as *any* writer
     * (including this one, even though it never writes to it) still
     * has the write end open, the read end never sees EOF.
     */

    close(pipefd[1]);

    if (ret != 0)
    {
        close(pipefd[0]);
        return strdup("");
    }

    out = malloc(cap);
    if (out == NULL)
    {
        close(pipefd[0]);
        waitpid(pid, &status, 0);
        return strdup("");
    }

    for (; ; )
    {
        ssize_t n;

        if (len + 256 >= cap)
        {
            FAR char *grown;

            cap *= 2;
            grown = realloc(out, cap);
            if (grown == NULL)
            {
                break;
            }

            out = grown;
        }

        n = read(pipefd[0], out + len, cap - len - 1);
        if (n <= 0)
        {
            break;
        }

        len += (size_t)n;
    }

    close(pipefd[0]);
    waitpid(pid, &status, 0);

    /* POSIX command substitution strips *trailing* newlines only --
     * not other whitespace, and not newlines anywhere else in the
     * output.
     */

    while (len > 0 && out[len - 1] == '\n')
    {
        len--;
    }

    out[len] = '\0';
    return out;
}

/****************************************************************************
 * Given 's' pointing just after an opening '(', finds the matching
 * closing ')' -- tracking nested parens (so "$(echo $(pwd))" finds
 * the *outer* close, not the inner one) and quotes (so a ')' inside
 * '...'/"..." doesn't count, matching real shell behavior). Returns
 * NULL if unterminated.
 ****************************************************************************/

static FAR const char *find_matching_paren(FAR const char *s)
{
    int depth = 1;
    char quote = '\0';

    while (*s != '\0')
    {
        if (quote != '\0')
        {
            if (*s == quote)
            {
                quote = '\0';
            }
        }
        else if (*s == '\'' || *s == '"')
        {
            quote = *s;
        }
        else if (*s == '(')
        {
            depth++;
        }
        else if (*s == ')')
        {
            depth--;
            if (depth == 0)
            {
                return s;
            }
        }

        s++;
    }

    return NULL;
}

/* Same idea as find_matching_paren(), but for the simpler backtick
 * form -- no nesting concept for backticks at all (real shells handle
 * nested backticks via backslash-escaping, not depth-tracking; not
 * implemented here, same "basic form works, edge cases don't yet" as
 * the rest of this file).
 */

static FAR const char *find_matching_backtick(FAR const char *s)
{
    while (*s != '\0' && *s != '`')
    {
        s++;
    }

    return (*s == '`') ? s : NULL;
}

bool find_command_subst(FAR const char *p, FAR const char **cmd_start,
                         FAR const char **cmd_end, FAR const char **after)
{
    FAR const char *close;

    if (*p == '`')
    {
        close = find_matching_backtick(p + 1);
        if (close == NULL)
        {
            return false;
        }

        *cmd_start = p + 1;
        *cmd_end = close;
        *after = close + 1;
        return true;
    }

    if (p[0] == '$' && p[1] == '(')
    {
        close = find_matching_paren(p + 2);
        if (close == NULL)
        {
            return false;
        }

        *cmd_start = p + 2;
        *cmd_end = close;
        *after = close + 1;
        return true;
    }

    return false;
}
