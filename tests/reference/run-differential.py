#!/usr/bin/env python3
"""tests/reference/run-differential.py -- run the same shell script
through bash, dash, and vaporshell, and compare output.

    python3 run-differential.py --nuttx-dir /path/to/built/nuttx some.test [more.test ...]
    python3 run-differential.py --nuttx-dir /path/to/built/nuttx --all-smoosh

Dev-only tooling (like update-compat-log.py in vaporOS-nuttx), not
something that runs on vaporOS itself -- this orchestrates a booted
NuttX/sim instance from the host, it doesn't run inside one.

HONESTY ABOUT WHAT THIS CAN AND CAN'T DO RIGHT NOW: vaporshell doesn't
read a script file yet, only one interactive line at a time (see
vaporshell's own README). This harness drives it the only way that's
actually possible today -- typing each line of the test script into
its interactive prompt over a pty, the same technique used throughout
vaporOS's own development sessions. That means multi-line constructs
(if/then/fi, for loops, function definitions spanning lines) will NOT
work correctly -- vaporshell has no idea line 2 is "still part of"
line 1's `if`, so it just runs each line as its own, independent
command. That's not a bug in this harness to paper over; it's an
honest, direct measurement of the real gap this test suite is meant to
help close. Once vaporshell can actually read and execute a script
file as a whole (the next real milestone), this harness gets a second,
real mode instead of just the line-by-line one.

Given that gap, this harness does NOT claim a hard pass/fail verdict
for vaporshell. It shows bash's output (the reference), dash's output
(a free cross-check -- when bash and dash disagree, the test is
probably exercising a bash-ism, worth knowing on its own), and
vaporshell's raw output side by side, plus a best-effort, clearly-
heuristic "looks like a match" signal after stripping vaporshell's own
prompt/echo noise. Read the raw output for anything that heuristic
calls uncertain -- it's a starting point for a human, not a verdict.
"""

import argparse
import os
import pty
import re
import select
import subprocess
import sys
import time
from pathlib import Path

REFERENCE_DIR = Path(__file__).resolve().parent
SMOOSH_DIR = REFERENCE_DIR / "smoosh-shell"

# vaporshell's own prompt, and the ANSI "clear to end of line" sequence
# it emits -- confirmed directly from real vaporshell sessions
# throughout this project's own development. Stripped before the
# best-effort comparison; left alone in the raw output shown to a
# human.
PROMPT_RE = re.compile(r"vaporshell\$ ")
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
CR_RE = re.compile(r"\r+")


def run_native(shell, script_path, timeout=10):
    try:
        result = subprocess.run(
            [shell, str(script_path)],
            capture_output=True, text=True, timeout=timeout,
        )
        return result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired:
        return "", "(timed out)", None
    except FileNotFoundError:
        return "", f"({shell} not found)", None


def run_vaporshell(nuttx_dir, script_lines, boot_wait=3, per_line_wait=0.6):
    nuttx_bin = Path(nuttx_dir) / "nuttx"
    if not nuttx_bin.exists():
        return f"(no nuttx binary at {nuttx_bin})"

    master, slave = pty.openpty()
    pid = os.fork()
    if pid == 0:
        os.setsid()
        os.dup2(slave, 0)
        os.dup2(slave, 1)
        os.dup2(slave, 2)
        os.close(master)
        os.close(slave)
        os.chdir(str(nuttx_dir))
        os.execv(str(nuttx_bin), [str(nuttx_bin)])

    os.close(slave)

    def drain(timeout=per_line_wait):
        buf = b""
        while True:
            r, _, _ = select.select([master], [], [], timeout)
            if master in r:
                try:
                    buf += os.read(master, 4096)
                except OSError:
                    break
            else:
                break
        return buf

    time.sleep(boot_wait)
    drain()
    os.write(master, b"vaporshell\n")
    time.sleep(1.0)
    drain()

    output = b""
    for line in script_lines:
        os.write(master, (line + "\n").encode())
        output += drain()

    os.write(master, b"exit\n")
    time.sleep(0.5)
    drain()
    os.write(master, b"poweroff\n")
    time.sleep(1.5)
    try:
        os.kill(pid, 9)
    except OSError:
        pass

    return output.decode(errors="replace")


def normalize(text):
    text = ANSI_RE.sub("", text)
    text = PROMPT_RE.sub("", text)
    text = CR_RE.sub("", text)
    lines = [ln for ln in text.splitlines() if ln.strip() != ""]
    return lines


def heuristic_match(bash_out, vaporshell_out):
    """Best-effort only, per this file's own top-of-file honesty note:
    does every non-blank line bash produced show up, in order, as a
    substring of some line vaporshell produced? Not a real diff --
    just enough to flag the clearest cases, leaving genuine judgment
    calls for a human reading the raw output.
    """
    bash_lines = normalize(bash_out)
    vs_lines = normalize(vaporshell_out)

    idx = 0
    for bl in bash_lines:
        found = False
        while idx < len(vs_lines):
            if bl in vs_lines[idx]:
                found = True
                idx += 1
                break
            idx += 1
        if not found:
            return False
    return True


def run_one(test_path, nuttx_dir):
    print(f"\n{'=' * 70}")
    print(f"TEST: {test_path}")
    print("=" * 70)

    script_lines = [
        ln for ln in Path(test_path).read_text().splitlines()
        if ln.strip() and not ln.strip().startswith("#")
    ]

    bash_out, bash_err, bash_rc = run_native("bash", test_path)
    dash_out, dash_err, dash_rc = run_native("dash", test_path)

    print(f"\n--- bash (reference) [exit {bash_rc}] ---")
    print(bash_out, end="")
    if bash_err:
        print(f"(stderr) {bash_err}", end="")

    print(f"\n--- dash (cross-check) [exit {dash_rc}] ---")
    print(dash_out, end="")
    if dash_err:
        print(f"(stderr) {dash_err}", end="")

    if bash_out != dash_out:
        print("\n[note] bash and dash disagree -- likely exercises a "
              "bash-specific extension, not a pure POSIX behavior.")

    if nuttx_dir is None:
        print("\n--- vaporshell: skipped (no --nuttx-dir given) ---")
        return

    vaporshell_out = run_vaporshell(nuttx_dir, script_lines)
    print("\n--- vaporshell (raw, line-by-line interactive feed) ---")
    print(vaporshell_out, end="")

    verdict = heuristic_match(bash_out, vaporshell_out)
    print(f"\n[heuristic] looks like a match: {verdict} "
          "(best-effort only -- see this script's own top-of-file note)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tests", nargs="*", help="Test file(s) to run")
    parser.add_argument("--all-smoosh", action="store_true",
                         help="Run every vendored smoosh test")
    parser.add_argument("--nuttx-dir", default=None,
                         help="Path to a built nuttx/ directory (contains "
                              "the nuttx binary). Omit to skip vaporshell "
                              "and just compare bash vs dash.")
    args = parser.parse_args()

    tests = [Path(t) for t in args.tests]
    if args.all_smoosh:
        tests += sorted(SMOOSH_DIR.glob("*.test"))

    if not tests:
        parser.error("give test file(s), or pass --all-smoosh")

    for t in tests:
        run_one(t, args.nuttx_dir)


if __name__ == "__main__":
    main()
