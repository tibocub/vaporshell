/*
 * docs/vaporshell.md, Milestone 1: a minimal shell that actually runs
 * programs, not just echoes input back. Builds on Milestone 1 step 1
 * (proven: app registration + readline() reuse work) by adding the
 * pieces confirmed available directly against NuttX's own source
 * rather than assumed -- posix_spawnp()/CONFIG_LIBC_ENVPATH for PATH
 * resolution, chdir()/getcwd() for $PWD, setenv()/environ (a macro
 * on NuttX, get_environ_ptr() under the hood -- #include <stdlib.h>,
 * not <unistd.h>) for the environment.
 *
 * Deliberately still minimal: whitespace + basic quote tokenizing,
 * no pipes, no redirection, no variable expansion, no control flow.
 * Those need either a real grammar (mrsh, still an open spike per
 * the design doc) or meaningfully more hand-written parsing -- not
 * bundled into the same step as "can this run a program at all."
 *
 * A real limitation worth being upfront about, confirmed by checking
 * the actual generated apps/builtin/builtin_list.h from a real build:
 * NSH's own commands (ls, cat, pwd, cd, ...) are NOT separately
 * spawnable programs -- they're internal to nshlib's own command
 * table, not entries in the builtin-apps table posix_spawn resolves
 * against. Only separately-registered apps/external programs
 * (vhello, portable_wc, portable_cat, vlua, vi, vaporshell itself)
 * actually run through this right now. Typing `ls` here will fail
 * with "No such file or directory" until toybox (or something like
 * it) exists as real, separately-spawnable programs -- that's not a
 * bug in what's below, it's the actual current state of what's
 * spawnable, and it's a big part of why the toybox port matters
 * architecturally, not just for coverage.
 *
 * -c mode (`vaporshell -c "cmd"`) runs one command non-interactively
 * and exits with its status -- the same argv shape
 * apps/system/system/system.c already sends when
 * CONFIG_SYSTEM_SYSTEM_SHPATH points at something other than NSH
 * (confirmed directly in that file: argv = {shpath, "-c", cmd,
 * NULL}). Built as a normal shell feature on its own merits
 * (scripting, one-liners, eventually sourcing an rc file), not
 * one-off glue -- it just also happens to be what unlocks redirecting
 * system()/os.execute() here instead of NSH.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <errno.h>

#include "system/readline.h"

#define MAX_TOKENS 64
#define MAX_PWD    128

static int g_last_status = 0;

/****************************************************************************
 * Splits 'line' into tokens in place -- argv[] entries point directly
 * into 'line's own buffer, so 'line' must stay alive (and gets freed
 * by the caller, once) for as long as argv[] is used. Whitespace-
 * separated, with single/double quotes treated as one token each
 * (quote characters themselves stripped, nothing expanded inside
 * them -- deliberately not real POSIX quoting yet, see the file
 * header). Returns argc; argv[argc] is always NULL.
 ****************************************************************************/

static int tokenize(FAR char *line, FAR char *argv[], int max_tokens)
{
    int argc = 0;
    FAR char *p = line;

    /* readline()'s own doc comment claims "the final newline removed,
     * so only the text of the line remains" -- confirmed directly
     * against readline_fd.c that this is not actually true in the
     * code we're calling: readline_fd()'s own doc comment says "if a
     * newline is read, it is stored into the buffer," and the
     * public readline() wrapper does zero post-processing of what
     * readline_fd() returns before handing it back. Every line here
     * really does end in '\n'. Treating '\n' (and defensively '\r',
     * given this project's whole CR/LF history with vterm_fb) as a
     * delimiter, same as space/tab, handles this at the one place it
     * actually matters rather than requiring a separate strip-the-
     * newline preprocessing step that's easy to forget to call from
     * every caller (both the interactive loop and -c mode use this
     * same function).
     */

    for (; ; )
    {
        char quote = '\0';
        FAR char *start;

        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        {
            p++;
        }

        if (*p == '\0')
        {
            break;
        }

        if (*p == '\'' || *p == '"')
        {
            quote = *p;
            p++;
        }

        start = p;

        if (quote != '\0')
        {
            while (*p != '\0' && *p != quote)
            {
                p++;
            }
        }
        else
        {
            while (*p != '\0' && *p != ' ' && *p != '\t' &&
                   *p != '\n' && *p != '\r')
            {
                p++;
            }
        }

        if (argc >= max_tokens - 1)
        {
            break;
        }

        argv[argc++] = start;

        if (*p != '\0')
        {
            *p = '\0';
            p++;
        }
    }

    argv[argc] = NULL;
    return argc;
}

/****************************************************************************
 * Multicall dispatch table: applet names vaporshell can run via `tbx`
 * (the toybox port's own entry point, vapor_entry.c -- not toybox's
 * own main()/toybox_main(), which vapor_entry.c deliberately bypasses;
 * see that file's own comment for why). Invocation convention is
 * argv[0]="tbx" with the applet name as argv[1] (vapor_entry.c calls
 * toy_exec(argv+1), whose own argv[0] becomes the applet name) --
 * NOT the argv[0]-rewriting convention docs/vaporshell.md originally
 * described, which assumed toybox's own main()/toybox_main() dispatch
 * would be used directly; that assumption no longer holds now that
 * vapor_entry.c exists for the reasons documented there.
 *
 * Hand-maintained, not generated from toybox's own build config --
 * reasonable at this scope (10 applets so far); worth revisiting if
 * this list grows much longer. Kept in sync with toybox/Makefile's
 * own CSRCS list by hand for now.
 ****************************************************************************/

static const char *const g_toybox_applets[] =
{
    "true", "false", "echo", "pwd",
    "cat", "mkdir", "rmdir", "touch", "printf", "rm",
    NULL
};

static bool is_toybox_applet(FAR const char *name)
{
    int i;

    for (i = 0; g_toybox_applets[i] != NULL; i++)
    {
        if (strcmp(name, g_toybox_applets[i]) == 0)
        {
            return true;
        }
    }

    return false;
}

/****************************************************************************
 * Builtins have to live here regardless of how good NuttX's spawn
 * story is -- cd mutates *this* process's cwd, not a child's, so it
 * can never be a spawned program. Sets *handled so the caller knows
 * whether to fall through to posix_spawnp.
 ****************************************************************************/

static int run_builtin(int argc, FAR char *argv[], FAR bool *handled)
{
    *handled = true;

    if (strcmp(argv[0], "cd") == 0)
    {
        char cwd[MAX_PWD];

        if (argc < 2)
        {
            /* No $HOME concept sorted out yet (see docs/vaporshell.md)
             * -- requiring an argument rather than guessing is safer
             * than a wrong default.
             */

            fprintf(stderr, "cd: missing argument\n");
            return 1;
        }

        if (chdir(argv[1]) != 0)
        {
            fprintf(stderr, "cd: %s: %s\n", argv[1], strerror(errno));
            return 1;
        }

        if (getcwd(cwd, sizeof(cwd)) != NULL)
        {
            setenv("PWD", cwd, 1);
        }

        return 0;
    }

    *handled = false;
    return 0;
}

/****************************************************************************
 * Resolves and runs a single command: builtin first, then PATH
 * resolution + posix_spawnp for anything else. Returns the exit
 * status (or 127, the standard shell convention for "command not
 * found", if spawning failed outright).
 ****************************************************************************/

static int run_command(int argc, FAR char *argv[])
{
    bool handled;
    int status;
    pid_t pid;
    int ret;

    if (argc == 0)
    {
        return 0;
    }

    status = run_builtin(argc, argv, &handled);
    if (handled)
    {
        return status;
    }

    /* posix_spawnp, not posix_spawn: PATH resolution
     * (CONFIG_LIBC_ENVPATH) is what makes a bare command name work at
     * all instead of requiring a full path. Its own error convention
     * is unusual and easy to get backwards -- it returns 0 on success
     * or a positive errno value directly on failure, NOT -1 with
     * errno set the way most POSIX calls work.
     */

    ret = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);

    /* Fall back to the toybox multicall table only on ENOENT (command
     * not found via normal PATH resolution) -- a real installed
     * program with the same name should always win, the same
     * precedence a real Unix shell gives $PATH over any builtin
     * utility replacement. ENOENT confirmed directly as the real
     * error this path returns: it's what "ls: No such file or
     * directory" (strerror(ENOENT)) came from before this fallback
     * existed.
     */

    if (ret == ENOENT && is_toybox_applet(argv[0]))
    {
        FAR char *tbx_argv[MAX_TOKENS + 1];
        int i;

        /* tokenize()'s own bound already guarantees argc <=
         * MAX_TOKENS - 1, but check the actual array capacity here
         * rather than assume that bound can never change independently.
         */

        if (argc + 2 > (int)(sizeof(tbx_argv) / sizeof(tbx_argv[0])))
        {
            fprintf(stderr, "vaporshell: %s: too many arguments\n", argv[0]);
            return 1;
        }

        tbx_argv[0] = "tbx";
        for (i = 0; i < argc; i++)
        {
            tbx_argv[i + 1] = argv[i];
        }

        tbx_argv[argc + 1] = NULL;

        ret = posix_spawnp(&pid, "tbx", NULL, NULL, tbx_argv, environ);
    }

    if (ret != 0)
    {
        fprintf(stderr, "vaporshell: %s: %s\n", argv[0], strerror(ret));
        return 127;
    }

    ret = waitpid(pid, &status, 0);
    if (ret < 0)
    {
        fprintf(stderr, "vaporshell: waitpid: %s\n", strerror(errno));
        return 1;
    }

    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }

    return 1;
}

int main(int argc, FAR char *argv[])
{
    char cwd[MAX_PWD];

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
        FAR char *tokens[MAX_TOKENS];
        FAR char *cmd = strdup(argv[2]);
        int ntok;

        if (cmd == NULL)
        {
            return 1;
        }

        ntok = tokenize(cmd, tokens, MAX_TOKENS);
        g_last_status = run_command(ntok, tokens);
        free(cmd);
        return g_last_status;
    }

    for (; ; )
    {
        FAR char *line = readline("vaporshell$ ");
        FAR char *tokens[MAX_TOKENS];
        int ntok;

        if (line == NULL)
        {
            /* NULL means EOF (Ctrl+D) -- readline()'s own documented
             * behavior, not an error case to special-case around.
             */

            printf("\n");
            break;
        }

        ntok = tokenize(line, tokens, MAX_TOKENS);

        if (ntok > 0)
        {
            if (strcmp(tokens[0], "exit") == 0 ||
                strcmp(tokens[0], "quit") == 0)
            {
                free(line);
                break;
            }

            g_last_status = run_command(ntok, tokens);
        }

        free(line);
    }

    return g_last_status;
}
