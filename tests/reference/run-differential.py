#!/usr/bin/env python3
"""tests/reference/run-differential.py -- run the same shell script
through bash, dash, and vaporshell, and compare output.

    python3 run-differential.py --nuttx-dir /path/to/built/nuttx some.test [more.test ...]
    python3 run-differential.py --nuttx-dir /path/to/built/nuttx --all-own
    python3 run-differential.py --nuttx-dir /path/to/built/nuttx --all-smoosh

Dev-only tooling (like update-compat-log.py in vaporOS-nuttx), not
something that runs on vaporOS itself -- this orchestrates a booted
NuttX/sim instance from the host, it doesn't run inside one.

vaporshell is now genuinely scriptable (`vaporshell script.sh`), so
this drives it exactly that way: boot the sim, mount the built nuttx/
directory itself as hostfs (the test script is copied there first),
run `vaporshell /data/<script>`, capture stdout, poweroff. This
replaced an earlier version that fed a script in line-by-line over
vaporshell's own interactive prompt (back when script-file execution
didn't exist yet) -- that approach needed heavy prompt/echo
normalization and couldn't handle multi-line constructs (if/for/while)
at all; running the real script file needs neither.

'--nuttx-dir' must point at a built nuttx/ directory (contains the
nuttx binary) with CONFIG_SIM_HOSTFS/CONFIG_FS_HOSTFS enabled -- the
same build vaporshell's own development already uses.
"""

import argparse
import os
import pty
import re
import select
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REFERENCE_DIR = Path(__file__).resolve().parent
SMOOSH_DIR = REFERENCE_DIR / "smoosh-shell"
OWN_DIR = REFERENCE_DIR.parent / "own"

ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
CR_RE = re.compile(r"\r+")


def run_native(shell, script_path, timeout=10):
    """Runs inside a fresh, isolated temporary directory -- confirmed
    directly this is required, not optional: some real tests (smoosh's
    own semantics.pattern.rightbracket.test/semantics.pattern.hyphen.
    test, glob-matching edge cases) create fixture files/directories
    as a side effect of running at all. Without this, they land
    wherever this harness happened to be invoked from -- which, run
    from the repo root, means real, tracked source directories getting
    polluted with stray files like "file]"/"file-"/"filea". Passing
    script_path as an absolute path first, since the subprocess's own
    cwd is about to change out from under any relative one.
    """

    script_path = Path(script_path).resolve()

    try:
        with tempfile.TemporaryDirectory(prefix="vaporshell-difftest-") as tmpdir:
            result = subprocess.run(
                [shell, str(script_path)],
                capture_output=True, text=True, timeout=timeout,
                cwd=tmpdir,
            )
        return result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired:
        return "", "(timed out)", None
    except FileNotFoundError:
        return "", f"({shell} not found)", None


def run_vaporshell(nuttx_dir, script_path, boot_wait=3, run_wait=5):
    nuttx_dir = Path(nuttx_dir)
    nuttx_bin = nuttx_dir / "nuttx"
    if not nuttx_bin.exists():
        return f"(no nuttx binary at {nuttx_bin})"

    # hostfs mounts nuttx_dir itself (the cwd nuttx is launched from) --
    # drop the script there under a fixed name so it's reachable at a
    # known /data path regardless of the real test file's own name.
    staged = nuttx_dir / "difftest_input.sh"
    shutil.copy(script_path, staged)

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

    def drain(timeout):
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
    drain(0.5)
    os.write(master, b"mount -t hostfs -o fs=. /data\n")
    time.sleep(1.0)
    drain(0.5)

    os.write(master, b"vaporshell /data/difftest_input.sh\n")
    output = drain(run_wait)

    os.write(master, b"poweroff\n")
    time.sleep(1.5)
    drain(0.5)
    try:
        os.kill(pid, 9)
    except OSError:
        pass

    staged.unlink(missing_ok=True)

    text = output.decode(errors="replace")
    text = ANSI_RE.sub("", text)
    text = CR_RE.sub("", text)

    # First line is the shell's own echo of the command we typed to
    # invoke it, and the last is the NSH prompt it returns to
    # afterward -- both artifacts of driving this over an interactive
    # pty, not part of the script's own output.
    lines = text.splitlines()
    if lines and lines[0].strip() == "vaporshell /data/difftest_input.sh":
        lines = lines[1:]
    while lines and (lines[-1].strip() == "" or lines[-1].strip().startswith("nsh>")):
        lines.pop()

    return "\n".join(lines) + ("\n" if lines else "")


def run_one(test_path, nuttx_dir):
    print(f"\n{'=' * 70}")
    print(f"TEST: {test_path}")
    print("=" * 70)

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

    vaporshell_out = run_vaporshell(nuttx_dir, test_path)
    print("\n--- vaporshell ---")
    print(vaporshell_out, end="")

    verdict = "PASS" if vaporshell_out == bash_out else "DIFFERS"
    print(f"\n[verdict] {verdict}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tests", nargs="*", help="Test file(s) to run")
    parser.add_argument("--all-own", action="store_true",
                         help="Run every test in tests/own/")
    parser.add_argument("--all-smoosh", action="store_true",
                         help="Run every vendored smoosh test")
    parser.add_argument("--nuttx-dir", default=None,
                         help="Path to a built nuttx/ directory (contains "
                              "the nuttx binary, hostfs enabled). Omit to "
                              "skip vaporshell and just compare bash vs dash.")
    args = parser.parse_args()

    tests = [Path(t) for t in args.tests]
    if args.all_own:
        tests += sorted(OWN_DIR.glob("*.sh"))
    if args.all_smoosh:
        tests += sorted(SMOOSH_DIR.glob("*.test"))

    if not tests:
        parser.error("give test file(s), or pass --all-own/--all-smoosh")

    for t in tests:
        run_one(t, args.nuttx_dir)


if __name__ == "__main__":
    main()
