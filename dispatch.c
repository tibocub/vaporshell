/*
 * dispatch.c -- multicall dispatch table: command names vaporshell
 * can run via `tbx` (vapor_entry.c, tbx's own entry point -- not
 * toybox's own main()/toybox_main(), which vapor_entry.c deliberately
 * bypasses; see that file's own comment for why). Invocation
 * convention is argv[0]="tbx" with the command name as argv[1]
 * (vapor_entry.c calls toy_exec(argv+1) for toybox's own commands, or
 * nshports_dispatch() first for anything ported from NSH instead --
 * see vaporOS-coreutils' own nsh-ports/ directory) -- NOT the
 * argv[0]-rewriting convention docs/design.md originally described,
 * which assumed toybox's own main()/toybox_main() dispatch would be
 * used directly; that assumption no longer holds now that
 * vapor_entry.c exists for the reasons documented there.
 *
 * This table doesn't distinguish toybox-sourced from NSH-sourced names
 * at all -- deliberately: from vaporshell's own side, both are just
 * "names tbx knows how to run", the toybox-vs-nsh-ports split only
 * matters inside vaporOS-coreutils itself (for diffing against each
 * one's own upstream independently). See its own README for which is
 * which.
 *
 * Hand-maintained, not generated from any build config -- reasonable
 * at this scope (65 commands so far); worth revisiting if this list
 * grows much longer. Kept in sync with vaporOS-coreutils' own
 * Makefile/CSRCS list by hand for now.
 */

#include <nuttx/config.h>
#include <string.h>

#include "vaporshell.h"

const char *const g_tbx_commands[] =
{
    "true", "false", "echo", "pwd",
    "cat", "mkdir", "rmdir", "touch", "printf", "rm",
    "ls", "cp", "mv",
    "printenv",
    "basename", "dirname", "sleep", "which",
    "head", "tail", "wc", "tee", "cut", "uniq", "sort", "yes",
    "grep", "egrep", "fgrep", "sed", "tr", "ln", "cmp", "uname", "arch",
    "expr", "date", "chmod",
    "find", "xargs", "env", "nohup", "comm", "expand", "fold", "nl", "od",
    "paste", "split", "tty", "unlink", "rev", "tac", "truncate", "xxd",
    "mktemp", "md5sum", "sha1sum", "sha224sum", "sha256sum", "sha384sum",
    "sha512sum",
    "poweroff",
    "test", "[",
    NULL
};

bool is_tbx_command(FAR const char *name)
{
    int i;

    for (i = 0; g_tbx_commands[i] != NULL; i++)
    {
        if (strcmp(name, g_tbx_commands[i]) == 0)
        {
            return true;
        }
    }

    return false;
}
