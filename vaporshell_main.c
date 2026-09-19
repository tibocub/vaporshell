/*
 * docs/design.md, Milestone 1: a minimal shell that actually runs
 * programs, not just echoes input back. Split across several small
 * files (tokenize.c, dispatch.c, help.c, script.c, builtins.c,
 * exec.c, expand.c, line.c, pipeline.c, control.c, all sharing
 * vaporshell.h) rather than one growing file -- see each one's own
 * top-of-file comment for what it's responsible for; this file is
 * just the entry point and the interactive loop, delegating a whole
 * line to run_line() (line.c), or to run_control_construct()
 * (control.c) if it starts an if/then/elif/else/fi.
 *
 * A real limitation worth being upfront about here specifically:
 * -c mode (`vaporshell -c "cmd"`) runs one command non-interactively
 * and exits with its status -- the same argv shape
 * apps/system/system/system.c already sends when
 * CONFIG_SYSTEM_SYSTEM_SHPATH points at something other than NSH
 * (confirmed directly in that file: argv = {shpath, "-c", cmd,
 * NULL}). Built as a normal shell feature on its own merits
 * (scripting, one-liners, eventually sourcing an rc file), not
 * one-off glue -- it just also happens to be what unlocks redirecting
 * system()/os.execute() here instead of NSH. Any other argument is a
 * script file to run non-interactively instead (see script.c).
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "system/readline.h"
#include "vaporshell.h"
#include "control.h"

int g_last_status = 0;
FAR const char *g_self_exe = "vaporshell";

/****************************************************************************
 * readline() never prints the prompt itself -- confirmed directly,
 * its own entry sequence has no prompt-printing logic at all, and
 * NuttX's own documentation for readline_prompt() says its purpose is
 * telling readline what to redraw for Home/End/history/tab-
 * completion, not the initial display. NSH does exactly this same
 * two-step sequence itself (nsh_session.c) -- explicit write() first,
 * then readline_prompt() separately for redraws. Without the write()
 * here, the prompt only ever appeared as a side effect of a redraw
 * (Home/End, ...), never on its own. Shared by the main loop below
 * and interactive_next_line() (this file's own next_line_fn, for
 * control.c's continuation prompt while an if/then/else spans
 * multiple lines) -- both need the identical prompt/readline dance,
 * just with a different prompt string.
 ****************************************************************************/

static FAR char *read_one_line(FAR const char *prompt)
{
    write(STDOUT_FILENO, prompt, strlen(prompt));
#if defined(CONFIG_READLINE_TABCOMPLETION) || defined(CONFIG_READLINE_EDIT)
    readline_prompt(prompt);
#endif
    return readline(prompt);
}

/* control.c's own next_line_fn for interactive use -- 'ctx' is unused
 * (nothing to pass; there's only one input stream, stdin, same as
 * readline() itself already assumes). 'continuation' is always true
 * here in practice: the *first* line of any construct is already read
 * by the main loop below before it ever calls
 * run_control_construct(), so this function only ever gets called for
 * the lines after that.
 */

static FAR char *interactive_next_line(FAR void *ctx, bool continuation)
{
    (void)ctx;
    (void)continuation;

    return read_one_line("> ");
}

int main(int argc, FAR char *argv[])
{
    char cwd[MAX_PWD];

#ifdef VAPORSHELL_POSIX
    /* Has to happen before anything can chdir() -- argv[0] may be a
     * relative path. Failure keeps the default name; see
     * posix/self_path.c for why that default is only right on NuttX.
     */

    FAR char *self = vs_resolve_self(argv[0]);

    if (self != NULL)
    {
        g_self_exe = self;
    }
#endif

    /* $SHELL reflects what's actually running, same as any real
     * shell would set it to itself. $PWD synced to the real cwd at
     * startup so it's accurate even if whatever spawned vaporshell
     * never set it.
     */

    setenv("SHELL", "vaporshell", 1);

    if (getcwd(cwd, sizeof(cwd)) != NULL)
    {
        setenv("PWD", cwd, 1);
    }

    if (argc >= 3 && strcmp(argv[1], "-c") == 0)
    {
        FAR char *cmd = strdup(argv[2]);
        bool should_exit;

        if (cmd == NULL)
        {
            return 1;
        }

        /* should_exit is meaningless here -- this process is a
         * single-shot invocation ending right after this either way,
         * whether the command itself was "exit" or anything else.
         */

        g_last_status = run_line(cmd, &should_exit);
        free(cmd);
        return g_last_status;
    }

    if (argc >= 2)
    {
        /* Deliberately not gated behind checking the file actually
         * exists first: run_script_file()'s own fopen() failure path
         * already reports a real, useful error either way, so there's
         * no separate check worth duplicating here.
         */

        return run_script_file(argv[1]);
    }

    for (; ; )
    {
        FAR char *line;
        bool should_exit;

        line = read_one_line("vaporshell$ ");

        if (line == NULL)
        {
            /* NULL means EOF (Ctrl+D) -- readline()'s own documented
             * behavior, not an error case to special-case around.
             */

            printf("\n");
            break;
        }

        if (is_control_start(line))
        {
            g_last_status = run_control_construct(line, interactive_next_line,
                                                    NULL, &should_exit);
        }
        else
        {
            g_last_status = run_line(line, &should_exit);
            free(line);
        }

        if (should_exit)
        {
            break;
        }
    }

    return g_last_status;
}
