#!/usr/bin/env python3
"""tests/coverage/gen-docs.py -- regenerate docs/bash-coverage.md and
docs/posix-coverage.md.

    python3 tests/coverage/gen-docs.py build/vaporshell [--bash PATH] [--dash PATH]

The prose lives in templates/*.md; this fills in the parts that must not be
hand-written because they are measurements:

    <!-- REFERENCES -->     which reference shells (and versions) ran
    <!-- PROBE-TABLES -->   one table per category, status per probe
    <!-- BUILTINS -->       builtin inventory against the reference
    <!-- SUMMARY -->        totals

Re-run it after a bash release (point --bash at the new build), commit the
regenerated docs, and the diff is exactly what changed.
"""
import argparse, datetime, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True      # do not leave __pycache__ in the repo
sys.path.insert(0, HERE)
import probe  # noqa: E402

CAT_TITLES = {
    "quoting": "Quoting", "param": "Parameter expansion", "expansion": "Other expansions",
    "split": "Field splitting", "glob": "Pathname expansion", "redirect": "Redirection",
    "control": "Compound commands, functions, pipelines", "test": "`test` / `[`",
    "builtin": "POSIX builtins", "bashvar": "Variables", "array": "Arrays",
    "bashbuiltin": "Bash builtins", "option": "Shell options", "syntax": "Syntax and misc",
    "echo": "echo", "printf": "printf", "hash": "hash", "times": "times", "ulimit": "ulimit",
    "alias": "alias", "lineno": "LINENO", "getopts": "getopts", "local": "local", "misc": "Other",
}
ORDER = ["quoting", "param", "expansion", "split", "glob", "redirect", "control", "test", "builtin",
         "echo", "printf", "getopts", "local", "hash", "alias", "times", "ulimit", "lineno",
         "option", "bashvar", "array", "bashbuiltin", "syntax", "misc"]

# POSIX utilities that are shell builtins (or must behave as if they were).
POSIX_SPECIAL = ["break", ":", "continue", ".", "eval", "exec", "exit", "export", "readonly",
                 "return", "set", "shift", "times", "trap", "unset"]
POSIX_REGULAR = ["alias", "bg", "cd", "command", "false", "fc", "fg", "getopts", "hash", "jobs",
                 "kill", "newgrp", "pwd", "read", "true", "type", "ulimit", "umask", "unalias",
                 "wait", "test", "[", "echo", "printf", "local"]


def builtin_state(shellcmd, name):
    """'special' / 'builtin' / 'function-or-other' / 'missing' via `type`."""
    try:
        p = subprocess.run(shellcmd + ["-c", "type %s 2>&1" % name], capture_output=True, text=True,
                           timeout=5, stdin=subprocess.DEVNULL, errors="replace")
    except Exception:
        return "missing"
    out = p.stdout.strip()
    if "special shell builtin" in out or "special built-in" in out:
        return "special"
    if "builtin" in out and "not found" not in out:
        return "builtin"
    if p.returncode == 0 and "not found" not in out and out:
        return "external"
    return "missing"


def gather(vs, bash, dash):
    probes = probe.load_probes(os.path.join(HERE, "probes.txt"))
    rows = []
    for p in probes:
        r = {"p": p}
        r["bash"] = probe.run([bash], p["code"]) if bash else None
        r["dash"] = probe.run([dash], p["code"]) if dash else None
        if p["ref"] == "bash" and bash:
            r["dash"] = probe.run([bash, "--posix"], p["code"])
        r["vb"] = probe.run([vs], p["code"])
        r["vp"] = probe.run([vs, "--posix"], p["code"])
        r["ok_b"] = None if r["bash"] is None else r["vb"] == r["bash"]
        r["ok_p"] = None if r["dash"] is None else r["vp"] == r["dash"]
        rows.append(r)
    return rows


def mark(ok):
    return "n/a" if ok is None else ("ok" if ok else "**differs**")


def tables(rows, which):
    key = "ok_b" if which == "bash" else "ok_p"
    out = []
    cats = [c for c in ORDER if any(r["p"]["cat"] == c for r in rows)]
    cats += sorted({r["p"]["cat"] for r in rows} - set(cats))
    for c in cats:
        rs = [r for r in rows if r["p"]["cat"] == c]
        good = sum(1 for r in rs if r[key])
        out.append("### %s  (%d/%d)\n" % (CAT_TITLES.get(c, c), good, len(rs)))
        out.append("| probe | %s | note |\n|---|---|---|" % ("bash mode" if which == "bash" else "POSIX mode"))
        for r in rs:
            out.append("| `%s` | %s | %s |" % (r["p"]["name"], mark(r[key]), r["p"]["note"]))
        out.append("")
    return "\n".join(out)


def builtins_bash(vs, bash):
    p = subprocess.run([bash, "-c", "compgen -b"], capture_output=True, text=True)
    names = sorted(set(p.stdout.split()))
    have, missing = [], []
    for n in names:
        (have if builtin_state([vs], n) in ("special", "builtin") else missing).append(n)
    return names, have, missing


def builtins_posix(vs, dash):
    lines = ["| utility | dash | vaporshell --posix |", "|---|---|---|"]
    gaps = 0
    for label, group in (("special built-ins", POSIX_SPECIAL), ("regular utilities", POSIX_REGULAR)):
        lines.append("| **%s** | | |" % label)
        for n in group:
            d = builtin_state([dash], n) if dash else "n/a"
            v = builtin_state([vs, "--posix"], n)
            # a gap only where dash has it as a builtin and we do not
            note = " (gap)" if d in ("special", "builtin") and v not in ("special", "builtin") else ""
            gaps += note != ""
            lines.append("| `%s` | %s | %s%s |" % (n, d, v, note))
    return "\n".join(lines), gaps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("vaporshell")
    ap.add_argument("--bash", default=shutil.which("bash"))
    ap.add_argument("--dash", default=shutil.which("dash"))
    ap.add_argument("--out", default=os.path.join(HERE, "..", "..", "docs"))
    a = ap.parse_args()
    vs = os.path.abspath(a.vaporshell)

    bash_v = probe.version([a.bash])
    dash_v = probe.version([a.dash])
    rows = gather(vs, a.bash, a.dash)
    n = len(rows)
    nb = sum(1 for r in rows if r["ok_b"])
    npx = sum(1 for r in rows if r["ok_p"])
    today = datetime.date.today().isoformat()

    refs = ("- bash-mode reference: `%s`\n- POSIX-mode reference: `%s`\n- generated: %s by "
            "`tests/coverage/gen-docs.py`\n" % (bash_v, dash_v, today))

    # ---- bash doc
    names, have, missing = builtins_bash(vs, a.bash)
    bash_bi = ("bash %s has %d builtins. vaporshell (bash mode) provides %d of them.\n\n"
               "**Missing (%d):** %s\n\n**Provided (%d):** %s\n" %
               (bash_v.split(",")[1].strip().split()[1] if "," in bash_v else "", len(names), len(have),
                len(missing), " ".join("`%s`" % m for m in missing),
                len(have), " ".join("`%s`" % h for h in have)))
    t = open(os.path.join(HERE, "templates", "bash-coverage.md")).read()
    t = (t.replace("<!-- REFERENCES -->", refs)
          .replace("<!-- SUMMARY -->", "Probes matching bash in bash mode: **%d of %d**.\n" % (nb, n))
          .replace("<!-- PROBE-TABLES -->", tables(rows, "bash"))
          .replace("<!-- BUILTINS -->", bash_bi))
    open(os.path.join(a.out, "bash-coverage.md"), "w").write(t)

    # ---- posix doc
    inv, gaps = builtins_posix(vs, a.dash)
    t = open(os.path.join(HERE, "templates", "posix-coverage.md")).read()
    t = (t.replace("<!-- REFERENCES -->", refs)
          .replace("<!-- SUMMARY -->", "Probes matching dash in POSIX mode: **%d of %d**. POSIX utilities "
                   "not provided as builtins: **%d**.\n" % (npx, n, gaps))
          .replace("<!-- PROBE-TABLES -->", tables(rows, "posix"))
          .replace("<!-- BUILTINS -->", inv))
    open(os.path.join(a.out, "posix-coverage.md"), "w").write(t)
    print("bash mode %d/%d, POSIX mode %d/%d; wrote docs/bash-coverage.md, docs/posix-coverage.md"
          % (nb, n, npx, n))


if __name__ == "__main__":
    main()
