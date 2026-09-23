#!/usr/bin/env python3
"""tests/nuttx-sim-smoke.py -- run vaporshell inside the NuttX simulator.

    python3 tests/nuttx-sim-smoke.py ../nuttx/nuttx

Boots the sim (built with `make -f dev.mk build`), starts vaporshell from
NSH through a pty, runs a list of commands and checks that each expected
line appears in the output. Covers what only the NuttX platform layer
exercises: posix_spawnp on bare names, the tbx fallback, command
substitution through a child `vaporshell -c`, pipelines of external
programs, and the deliberate "not supported on this platform yet" cases.
Exit status is the number of failed checks (0 = all good).
"""
import os
import pty
import select
import sys
import time

PROMPT = "vaporshell$ "

# (command, line that must appear in the output after the echoed command)
CASES = [
    ("echo hello", "hello"),
    ("x=5; echo x is $x", "x is 5"),
    ("echo $(echo inner)", "inner"),
    ("echo `echo tick` $((2+3))", "tick 5"),
    ("x=$(exit 7); echo rc=$?", "rc=7"),
    ("f() { echo in f: $1; return 4; }; f arg; echo rc=$?", "rc=4"),
    ("true | false; echo rc=$?", "rc=1"),
    ("echo one two | wc -w", "2"),
    ("echo written > /tmp/vs_smoke; echo more >> /tmp/vs_smoke; cat /tmp/vs_smoke", "more"),
    ("[ -d /tmp ] && echo dir-ok", "dir-ok"),
    ("case abc in a*) echo case-ok;; esac", "case-ok"),
    ("for i in 1 2 3; do echo n$i; done", "n3"),
    ("nosuchcmd; echo rc=$?", "rc=127"),
    ("type nosuch; echo rc=$?", "rc=1"),
    ("( echo x )", "x"),
    ("x=1; ( x=2 ); echo x=$x", "x=1"),
    ("f() { echo in-f; }; y=$(f); echo $y", "in-f"),
    ("echo abc | { read v; echo got:$v; }", "got:abc"),
    ("fn() { i=0; while [ $i -lt 200 ]; do echo row$i-padding-padding; i=$((i+1)); done; }; fn | wc -l | { read n rest; echo lines=$n; }", "lines=200"),
    ("f() { :; }; f &", "vaporshell: &: only an external program can run in the background on this platform (no fork)"),
    ("echo bg & wait", "bg"),
    ("sleep 1 & echo started; wait; echo waited", "waited"),
    ("trap 'echo sig' USR1; kill -USR1 $$; echo after", "after"),
    ("trap 'echo bye' EXIT; echo trap-set", "trap-set"),
]


def main():
    if len(sys.argv) != 2 or not os.access(sys.argv[1], os.X_OK):
        print(__doc__)
        return 2
    exe = os.path.abspath(sys.argv[1])
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(os.path.dirname(exe))
        os.execv(exe, [exe])

    def read(timeout, until):
        out, end = b"", time.time() + timeout
        while time.time() < end:
            if select.select([fd], [], [], 0.2)[0]:
                try:
                    data = os.read(fd, 65536)
                except OSError:
                    break
                if not data:
                    break
                out += data
                if until.encode() in out:
                    break
        return out.decode(errors="replace").replace("\r", "").replace("\x1b[K", "")

    def send(cmd, timeout=4, until=PROMPT):
        os.write(fd, (cmd + "\n").encode())
        return read(timeout, until)

    read(8, "nsh> ")
    out = send("vaporshell", 4)
    if PROMPT not in out:
        print("could not start vaporshell from NSH:", repr(out))
        os.kill(pid, 9)
        return 1

    failures = 0
    for cmd, expect in CASES:
        lines = [l.strip() for l in send(cmd).split("\n")]
        lines = [l for l in lines if l and l != cmd and not l.startswith(PROMPT.strip())]
        if expect in lines:
            print("ok    " + cmd)
        else:
            failures += 1
            print("FAIL  " + cmd)
            print("      wanted line: " + expect)
            print("      got: " + " | ".join(lines))

    send("exit", 3, "nsh> ")
    os.write(fd, b"poweroff\n")
    time.sleep(1)
    try:
        os.kill(pid, 9)
    except ProcessLookupError:
        pass
    print("%d failed" % failures if failures else "all passed")
    return failures


if __name__ == "__main__":
    sys.exit(main())
