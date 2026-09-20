#!/usr/bin/env python3
"""tests/coverage/probe.py -- measure vaporshell against its reference shells.

    python3 tests/coverage/probe.py build/vaporshell [--bash PATH] [--dash PATH]
                                    [--markdown FILE] [--only SUBSTR] [--all]

Runs every probe in probes.txt under

    bash <ver>            the bash-mode reference  (default: bash on PATH)
    dash <ver>            the POSIX-mode reference (default: dash on PATH)
    vaporshell            bash mode   -> compared with bash
    vaporshell --posix    POSIX mode  -> compared with dash

and prints where vaporshell differs from its reference. Nothing here is
hand-written expectation: the references decide. That is what lets the
coverage documents (docs/bash-coverage.md, docs/posix-coverage.md) be
re-derived after a new bash release: build it, run this with --bash, read
the diff of the report.

A probe is a name, a category and a script; scripts run in a fresh temp
directory with stdin closed. Compared: stdout and exit status.
"""
import argparse, os, re, shutil, subprocess, sys, tempfile

def load_probes(path):
    probes, cur = [], None
    for line in open(path):
        line = line.rstrip("\n")
        m = re.match(r"^## (\S+) \| (\S+)\s*(?:\| (.*))?$", line)
        if m:
            note = m.group(3) or ""
            ref = "dash"
            if "posix-ref=bash" in note:
                ref = "bash"           # dash lacks the feature: POSIX text / bash decide
                note = note.replace("posix-ref=bash", "").strip(" ;,")
            cur = {"name": m.group(1), "cat": m.group(2), "note": note, "ref": ref, "code": []}
            probes.append(cur)
        elif cur is not None and not line.startswith("#!"):
            cur["code"].append(line)
    for p in probes:
        p["code"] = "\n".join(p["code"]).strip("\n") + "\n"
    return probes

def run(cmd, code, timeout=8):
    with tempfile.TemporaryDirectory() as root:
        # a fixed leaf name: probes must not depend on the random part of the path
        d = os.path.join(root, "w")
        os.mkdir(d)
        try:
            p = subprocess.run(cmd + ["-c", code], cwd=d, stdin=subprocess.DEVNULL, capture_output=True,
                               text=True, timeout=timeout, errors="replace")
            return p.stdout, p.returncode
        except subprocess.TimeoutExpired:
            return "(timeout)", -1

def version(cmd):
    try:
        out = subprocess.run(cmd + ["--version"], capture_output=True, text=True).stdout.splitlines()
        if out and out[0]:
            return out[0]
    except Exception:
        pass
    # dash has no --version: ask the package manager (dpkg, then rpm)
    try:
        out = subprocess.run(["dpkg", "-s", "dash"], capture_output=True, text=True).stdout
        m = re.search(r"^Version: (.*)$", out, re.M)
        if m:
            return "dash " + m.group(1)
    except Exception:
        pass
    try:
        out = subprocess.run(["rpm", "-q", "dash"], capture_output=True, text=True).stdout.strip()
        if out and "not installed" not in out:
            return out
    except Exception:
        pass
    return os.path.basename(cmd[0])

def short(s, n=34):
    s = s.replace("\n", "|")
    return s if len(s) <= n else s[: n - 1] + "…"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("vaporshell")
    ap.add_argument("--bash", default=shutil.which("bash"))
    ap.add_argument("--dash", default=shutil.which("dash"))
    ap.add_argument("--probes", default=os.path.join(os.path.dirname(__file__), "probes.txt"))
    ap.add_argument("--markdown")
    ap.add_argument("--only", default="")
    ap.add_argument("--all", action="store_true", help="show matching probes too")
    a = ap.parse_args()

    vs = os.path.abspath(a.vaporshell)
    probes = [p for p in load_probes(a.probes) if a.only in p["name"] or a.only in p["cat"]]
    bash_v = version([a.bash]) if a.bash else "(no bash)"
    dash_v = version([a.dash]) if a.dash else "(no dash)"
    print("bash-mode reference: %s\nposix-mode reference: %s\n" % (bash_v, dash_v))

    rows, bad_bash, bad_posix = [], 0, 0
    for p in probes:
        r = {"p": p}
        r["bash"] = run([a.bash], p["code"]) if a.bash else None
        r["dash"] = run([a.dash], p["code"]) if a.dash else None
        r["vb"] = run([vs], p["code"])
        r["vp"] = run([vs, "--posix"], p["code"])
        if p["ref"] == "bash" and a.bash:
            r["dash"] = run([a.bash, "--posix"], p["code"])   # shown in the dash row
        r["ok_b"] = r["bash"] is None or r["vb"] == r["bash"]
        r["ok_p"] = r["dash"] is None or r["vp"] == r["dash"]
        bad_bash += not r["ok_b"]
        bad_posix += not r["ok_p"]
        rows.append(r)
        if a.all or not (r["ok_b"] and r["ok_p"]):
            print("[%s] %s%s" % (p["cat"], p["name"], "  (%s)" % p["note"] if p["note"] else ""))
            plabel = "bash-p" if p["ref"] == "bash" else "dash  "
            for label, key, ok in (("bash  ", "bash", None), ("vs    ", "vb", r["ok_b"]),
                                   (plabel, "dash", None), ("vs -p ", "vp", r["ok_p"])):
                if r[key] is None:
                    continue
                o, rc = r[key]
                print("   %s %-38s rc=%d%s" % (label, short(o), rc, "" if ok is None or ok else "   <-- DIFFERS"))

    n = len(rows)
    print("\n%d probes: bash mode matches %d/%d, POSIX mode matches %d/%d" %
          (n, n - bad_bash, n, n - bad_posix, n))

    if a.markdown:
        with open(a.markdown, "w") as f:
            f.write("# Coverage report (generated)\n\n")
            f.write("Generated by `tests/coverage/probe.py`. Do not edit by hand.\n\n")
            f.write("- bash-mode reference: `%s`\n- posix-mode reference: `%s`\n\n" % (bash_v, dash_v))
            f.write("Legend: ok = vaporshell matches the reference (stdout and exit status); "
                    "**DIFF** = it does not.\n\n")
            cats = []
            for r in rows:
                if r["p"]["cat"] not in cats:
                    cats.append(r["p"]["cat"])
            for c in cats:
                f.write("## %s\n\n| probe | bash mode | POSIX mode | note |\n|---|---|---|---|\n" % c)
                for r in rows:
                    if r["p"]["cat"] != c:
                        continue
                    f.write("| `%s` | %s | %s | %s |\n" % (r["p"]["name"],
                            "ok" if r["ok_b"] else "**DIFF**", "ok" if r["ok_p"] else "**DIFF**", r["p"]["note"]))
                f.write("\n")
            f.write("Totals: bash mode %d/%d, POSIX mode %d/%d.\n" % (n - bad_bash, n, n - bad_posix, n))
    return 1 if (bad_bash or bad_posix) else 0

if __name__ == "__main__":
    sys.exit(main())
