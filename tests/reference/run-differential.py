#!/usr/bin/env python3
"""tests/reference/run-differential.py -- run the same shell script
through bash, dash, and vaporshell, and compare output.

    python3 run-differential.py --nuttx-dir /path/to/built/nuttx some.test [more.test ...]
    python3 run-differential.py --nuttx-dir /path/to/built/nuttx --all-own
    python3 run-differential.py --nuttx-dir /path/to/built/nuttx --all-smoosh
    python3 run-differential.py --nuttx-dir /path/to/built/nuttx --all-suites --brief

--all-suites runs the same four suites as tests/run-suites.sh (own and modes/,
in bash mode against bash and in --posix mode against dash), so NuttX gets
the coverage the Linux build has. --brief prints one line per test and full
output only for failures; --jobs N runs N simulators at once; --tally FILE
(or $VS_TALLY) appends counts for tests/check-all.sh; the exit status is 1 if
any test differs.

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
import concurrent.futures
import difflib
import itertools
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

# Colours: on for a terminal, off for pipes/files; NO_COLOR / FORCE_COLOR as in
# tests/lib.sh.
if (sys.stdout.isatty() and not os.environ.get("NO_COLOR")) or os.environ.get("FORCE_COLOR"):
    red, green, yellow, bold, reset = '\033[91m', '\033[92m', '\033[93m', '\033[1m', '\033[0m'
else:
    red = green = yellow = bold = reset = ''

REFERENCE_DIR = Path(__file__).resolve().parent
SMOOSH_DIR = REFERENCE_DIR / "smoosh-shell"
OWN_DIR = REFERENCE_DIR.parent / "own"
MODES_DIR = REFERENCE_DIR.parent / "modes"

ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
CR_RE = re.compile(r"\r+")

# The reference shells; BASH_REF=/path/to/bash, DASH_REF=/path/to/dash select others.
# (Not $BASH: bash sets that itself -- see tests/run-suites.sh.)
REFS = {"bash": os.environ.get("BASH_REF", "bash"), "dash": os.environ.get("DASH_REF", "dash")}

# The suites tests/run-suites.sh runs: (name, directory, reference, vaporshell options).
SUITES = [
    ("own-bash",    OWN_DIR,            "bash", ""),
    ("own-posix",   OWN_DIR,            "dash", "--posix"),
    ("modes-bash",  MODES_DIR / "bash", "bash", ""),
    ("modes-posix", MODES_DIR / "posix", "dash", "--posix"),
]

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
        return "", f"{red}(timed out){reset}", None
    except FileNotFoundError:
        return "", f"({red}{shell} not found{reset})", None


def wait_for(master_fd, needle, timeout, poll_interval=0.05):
    """Reads from master_fd, in short poll_interval steps rather than
    one long fixed sleep, until 'needle' (bytes) appears in the
    accumulated output or 'timeout' seconds elapse. Returns as soon as
    the expected output actually shows up -- boot, a mount, and a
    script run each take genuinely different, usually much-less-than-
    'timeout' amounts of real time, and a fixed sleep() pays the same,
    worst-case cost every single time regardless. 'timeout' is a
    safety net for a real hang, not the normal-case wait -- confirmed
    directly this was the actual, whole cause of vaporshell's own test
    runs looking slow: the previous version's fixed sleeps (boot_wait=3
    + 1.0 + run_wait=5 + 1.5 = 10.5s) ran to completion on *every* test
    regardless of how fast NuttX/vaporshell genuinely responded, which
    is instant interactively -- nothing about vaporshell itself was
    ever slow.
    """

    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        remaining = deadline - time.time()
        r, _, _ = select.select([master_fd], [], [], min(poll_interval, remaining))
        if master_fd in r:
            try:
                chunk = os.read(master_fd, 4096)
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            if needle in buf:
                break
    return buf


_STAGE_IDS = itertools.count()


def run_vaporshell(nuttx_dir, script_path, vs_opts="", boot_timeout=8, cmd_timeout=8,
                   run_timeout=15):
    nuttx_dir = Path(nuttx_dir)
    nuttx_bin = nuttx_dir / "nuttx"
    if not nuttx_bin.exists():
        return f"{red}(no nuttx binary at {nuttx_bin}){reset}"

    # hostfs mounts nuttx_dir itself (the cwd nuttx is launched from) --
    # drop the script there under a name that is unique to this run (several
    # simulators may be running at once) so it is reachable at a known /data
    # path regardless of the real test file's own name.
    staged_name = f"difftest_input.{os.getpid()}.{next(_STAGE_IDS)}.sh"
    staged = nuttx_dir / staged_name
    shutil.copy(script_path, staged)

    master, slave = pty.openpty()
    proc = subprocess.Popen([str(nuttx_bin)], stdin=slave, stdout=slave, stderr=slave,
                            cwd=str(nuttx_dir), start_new_session=True, close_fds=True)
    os.close(slave)

    try:
        wait_for(master, b"nsh> ", boot_timeout)
        os.write(master, b"mount -t hostfs -o fs=. /data\n")
        wait_for(master, b"nsh> ", cmd_timeout)

        # The native references have stderr dropped (error wording is
        # shell-specific and is not what these tests check), but the NuttX pty
        # merges stderr into stdout. So the test runs inside a wrapper shell
        # that discards its stderr, the way the native runs do.
        #
        # The output is delimited by markers the *program* prints. Guessing
        # which lines are the console's echo of our command and which are the
        # test's output does not work: the echo comes back garbled.
        tag = f"{os.getpid()}x{next(_STAGE_IDS)}"
        mark_s, mark_e = f"@@S{tag}@@", f"@@E{tag}@@"
        inner = f"vaporshell {vs_opts + ' ' if vs_opts else ''}/data/{staged_name} 2>/dev/null"
        command = f'vaporshell -c "echo {mark_s}; {inner}; echo -n {mark_e}"'
        os.write(master, command.encode() + b"\n")
        # wait_for() starts each call with a fresh, empty local buffer --
        # bytes already read by the mount step's own wait_for() above are
        # consumed, not re-delivered -- so this correctly waits for the
        # *next* "nsh> " (vaporshell's own completion), not the one
        # already handled above.
        output = wait_for(master, b"nsh> ", run_timeout, poll_interval=0.02)

        os.write(master, b"poweroff\n")
        time.sleep(0.3)  # poweroff itself is near-instant; just a brief,
                          # fixed drain before the kill below, not worth
                          # adaptive waiting for
    finally:
        try:
            proc.kill()
        except OSError:
            pass
        proc.wait()                     # reap it: no zombie per test
        os.close(master)
        staged.unlink(missing_ok=True)

    text = output.decode(errors="replace")
    text = ANSI_RE.sub("", text)
    text = CR_RE.sub("", text)

    # Everything between the two markers is the test's own stdout. The marker
    # lines are exact lines of their own; the echoed command line also contains
    # the marker text but never as a whole line. (A test that never finishes --
    # a hang, a crash -- has no end marker: report what there is, flagged.)
    start = text.rfind("\n" + mark_s + "\n")
    if start >= 0:
        body = text[start + len(mark_s) + 2:]
        end = body.find(mark_e)
        if end >= 0:
            return body[:end]
        return body.rstrip("\n").rsplit("\nnsh>", 1)[0] + f"\n{red}(no end marker: hung or crashed){reset}\n"
    return f"{red}(no start marker: the shell never started the test){reset}\n" + text


# What the NuttX build cannot do (yet), as tokens a test names in a
# "# requires:" line near its top. A test that needs one is reported as
# SKIP on NuttX instead of a DIFFERS that is not a shell bug:
#   async         `&` on anything but an external program (no fork, and an
#                 in-process subshell cannot run concurrently)
#   signals       trap on real signals, kill
#   wc-format     toybox wc prints a file name for stdin; output differs from GNU wc
#   cmd:NAME      an external program the NuttX image does not have
#   env:NAME      an environment variable NuttX does not set (HOME, HOSTNAME)
#   float         printf %f/%e/%g: libc float support is CONFIG_LIBC_FLOATINGPOINT
#   path-lookup   NuttX finds builtin apps regardless of $PATH
NUTTX_LACKS = {"async", "signals", "wc-format", "env:HOME", "env:HOSTNAME", "float",
               "path-lookup",
               "cmd:sed", "cmd:tr", "cmd:tail", "cmd:mktemp", "cmd:ln"}


def requirements(test_path):
    reqs = set()
    with open(test_path, errors="replace") as f:
        for _ in range(10):
            line = f.readline()
            if line.startswith("# requires:"):
                reqs.update(line.split(":", 1)[1].split())
    return reqs


def compute(test_path, nuttx_dir, ref, vs_opts):
    """Runs one test everywhere it should run; returns a result dict."""
    r = {"path": Path(test_path), "ref": ref, "opts": vs_opts}
    r["ref_out"], r["ref_err"], r["ref_rc"] = run_native(REFS[ref], test_path)
    other = "dash" if ref == "bash" else "bash"
    r["other_out"], r["other_err"], r["other_rc"] = run_native(REFS[other], test_path)
    r["other"] = other

    if nuttx_dir is None:
        r["verdict"] = "none"
        return r

    missing = requirements(test_path) & NUTTX_LACKS
    if missing:
        r["verdict"] = "skip"
        r["missing"] = sorted(missing)
        return r

    r["vs_out"] = run_vaporshell(nuttx_dir, test_path, vs_opts)
    r["verdict"] = "pass" if r["vs_out"] == r["ref_out"] else "fail"
    return r


def show_verbose(r):
    print(f"\n{'=' * 70}")
    print(f"TEST: {r['path']}")
    print("=" * 70)
    print(f"\n--- {r['ref']} (reference) [exit {r['ref_rc']}] ---")
    print(r["ref_out"], end="")
    if r["ref_err"]:
        print(f"(stderr) {r['ref_err']}", end="")
    print(f"\n--- {r['other']} (cross-check) [exit {r['other_rc']}] ---")
    print(r["other_out"], end="")
    if r["other_err"]:
        print(f"(stderr) {r['other_err']}", end="")
    if r["ref_out"] != r["other_out"]:
        print(f"\n{yellow}[note] bash and dash disagree -- likely exercises a "
              f"bash-specific extension, not a pure POSIX behavior.{reset}")
    if r["verdict"] == "none":
        print("\n--- vaporshell: skipped (no --nuttx-dir given) ---")
    elif r["verdict"] == "skip":
        print(f"\n--- vaporshell: {yellow}SKIP{reset} on NuttX (needs: {' '.join(r['missing'])}) ---")
    else:
        print("\n--- vaporshell ---")
        print(r["vs_out"], end="")
        print(f"\n[verdict] {green + 'PASS' if r['verdict'] == 'pass' else red + 'DIFFERS'}{reset}")


def show_brief(r):
    name = r["path"].name
    if r["verdict"] == "pass":
        print(f"{green}ok{reset}    {name}")
    elif r["verdict"] == "skip":
        print(f"{yellow}skip{reset}  {name} (needs: {' '.join(r['missing'])})")
    elif r["verdict"] == "fail":
        print(f"{red}FAIL{reset}  {name}")
        diff = list(difflib.unified_diff(r["ref_out"].splitlines(), r["vs_out"].splitlines(),
                                         f"{r['ref']}", "vaporshell", lineterm="", n=1))
        for line in diff[:24]:
            print("      " + line)
        if len(diff) > 24:
            print(f"      ... ({len(diff) - 24} more lines)")


def run_group(label, tests, nuttx_dir, ref, vs_opts, args, tally):
    """Runs a list of tests, printing results in order; returns (pass, fail, skip)."""
    counts = {"pass": 0, "fail": 0, "skip": 0, "none": 0}
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for r in pool.map(lambda t: compute(t, nuttx_dir, ref, vs_opts), tests):
            (show_brief if args.brief else show_verbose)(r)
            counts[r["verdict"]] += 1
            if r["verdict"] == "fail" and tally:
                with open(tally, "a") as f:
                    f.write(f"F\t{label}\t{r['path'].name}\n")
    if tally:
        with open(tally, "a") as f:
            f.write(f"G\t{label}\t{counts['pass']}\t{counts['fail']}\t{counts['skip']}\n")
    if args.brief or len(tests) > 1:
        color = red if counts["fail"] else green
        skipped = f", {counts['skip']} skipped" if counts["skip"] else ""
        print(f"{color}{counts['pass']} passed, {counts['fail']} failed{skipped}  [{label}]{reset}")
    return counts["pass"], counts["fail"], counts["skip"]


def check_bash_ref():
    """The bash reference must be bash in its default mode, not bash started as sh."""
    try:
        out = subprocess.run([REFS["bash"], "--version"], capture_output=True, text=True).stdout
        posix = subprocess.run([REFS["bash"], "-c", "case $SHELLOPTS in *posix*) exit 1;; esac"]).returncode
    except FileNotFoundError:
        sys.exit(f"BASH_REF ({REFS['bash']}) not found")
    if not out.startswith("GNU bash") or posix != 0:
        sys.exit(f"BASH_REF ({REFS['bash']}) is not GNU bash in its default mode; set BASH_REF=/path/to/bash")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tests", nargs="*", help="Test file(s) to run")
    parser.add_argument("--all-own", action="store_true", help="Run every test in tests/own/ (vs bash)")
    parser.add_argument("--all-smoosh", action="store_true", help="Run every vendored smoosh test")
    parser.add_argument("--all-suites", action="store_true",
                        help="Run the four suites tests/run-suites.sh runs, each against its reference")
    parser.add_argument("--suite", action="append", choices=[n for n, *_ in SUITES],
                        help="With --all-suites: only this suite (repeatable)")
    parser.add_argument("--only", default="", help="Only tests whose file name contains this")
    parser.add_argument("--brief", action="store_true",
                        help="One line per test; full output only for failures")
    parser.add_argument("--jobs", type=int, default=1, help="Simulators to run at once (default 1)")
    parser.add_argument("--tally", default=os.environ.get("VS_TALLY"),
                        help="Append G/F count lines here (default: $VS_TALLY)")
    parser.add_argument("--nuttx-dir", default=None,
                        help="Path to a built nuttx/ directory (contains the nuttx binary, hostfs "
                             "enabled). Omit to skip vaporshell and just compare bash vs dash.")
    args = parser.parse_args()

    groups = []   # (label, tests, ref, opts)
    if args.all_suites:
        for name, d, ref, opts in SUITES:
            if args.suite and name not in args.suite:
                continue
            groups.append((f"NuttX {name}", sorted(d.glob("*.sh")), ref, opts))
    if args.all_own:
        groups.append(("NuttX own-bash", sorted(OWN_DIR.glob("*.sh")), "bash", ""))
    if args.all_smoosh:
        groups.append(("NuttX smoosh", sorted(SMOOSH_DIR.glob("*.test")), "bash", ""))
    if args.tests:
        groups.append(("NuttX selected", [Path(t) for t in args.tests], "bash", ""))
    if not groups:
        parser.error("give test file(s), or pass --all-own/--all-smoosh/--all-suites")

    check_bash_ref()
    total = {"fail": 0, "pass": 0, "skip": 0}
    for label, tests, ref, opts in groups:
        tests = [t for t in tests if args.only in Path(t).name]
        p, f, k = run_group(label, tests, args.nuttx_dir, ref, opts, args, args.tally)
        total["pass"] += p
        total["fail"] += f
        total["skip"] += k

    if not args.brief:
        if total["fail"] == 0:
            print(f"\n{green}All tests passed{reset}\n")
        else:
            print(f"\n{red}{total['fail']} test(s) failed{reset}\n")
    if total["skip"]:
        print(f"{yellow}{total['skip']} skipped (NuttX cannot run them yet: see NUTTX_LACKS){reset}\n")
    sys.exit(1 if total["fail"] else 0)


if __name__ == "__main__":
    main()
