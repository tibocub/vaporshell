#!/usr/bin/env python3
"""tests/pty/run.py VAPORSHELL -- line editor tests through a pseudo-terminal.

Each test starts a fresh shell with prompt "$ ", types keys and checks the
screen (what a terminal would display) or the output of what was run.
"""

import atexit
import os
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptytest import Session  # noqa: E402

UP, DOWN, RIGHT, LEFT = "\x1b[A", "\x1b[B", "\x1b[C", "\x1b[D"
HOME, END, DEL = "\x1b[H", "\x1b[F", "\x1b[3~"
CTRL = lambda c: chr(ord(c) - 64)  # noqa: E731
ENTER = "\r"

VS = None
TMP = tempfile.mkdtemp(prefix="vs-pty-")
atexit.register(shutil.rmtree, TMP, True)
results = []


def shell(env=None, cols=80, rows=24, cwd=None, args=()):
    e = {"PS1": "$ ", "HISTFILE": os.path.join(TMP, "hist-%d" % time.time_ns()),
         "HOME": TMP, "PATH": os.environ["PATH"], "VASH_BRACKETED_PASTE": "1"}
    e.update(env or {})
    return Session([VS, *args], env=e, cols=cols, rows=rows, cwd=cwd or TMP)


def test(name):
    def deco(fn):
        fn.test_name = name
        results.append(fn)
        return fn
    return deco


def expect(s, lines):
    got = s.screen()
    if got != lines:
        raise AssertionError("screen:\n  %s\nexpected:\n  %s" %
                             ("\n  ".join(got), "\n  ".join(lines)))


@test("runs a typed command")
def _():
    s = shell()
    s.type("echo hi"); s.send(ENTER)
    expect(s, ["$ echo hi", "hi", "$"])
    s.close()


@test("Home/End/arrows/Delete/Backspace")
def _():
    s = shell()
    s.type("echo abcd")
    s.send(LEFT * 2 + "X")
    s.send(HOME + RIGHT * 5 + DEL)
    s.send(END + "\x7f" + "Z")
    s.send(ENTER)
    # "echo abcd" -> abcXd? cursor 2 left of end: "echo abXcd"... then
    # Home, 5 right (after "echo "), Delete 'a' -> "echo bXcd"; End, BS, Z
    expect(s, ["$ echo bXcZ", "bXcZ", "$"])
    s.close()


@test("Ctrl-A/E/B/F/K/U/W/Y")
def _():
    s = shell()
    s.type("echo one two")
    s.send(CTRL("W"))               # kills "two"
    s.send(CTRL("A") + CTRL("K"))   # kills everything
    s.send(CTRL("Y"))               # yanks "echo one two"? last kill = whole line
    s.send(ENTER)
    expect(s, ["$ echo one", "one", "$"])
    s.close()


@test("Ctrl-U then yank")
def _():
    s = shell()
    s.type("echo abc def")
    s.send(CTRL("A") + RIGHT * 5 + CTRL("U"))   # kills "echo "
    s.send(CTRL("E") + " " + CTRL("Y"))
    assert s.screen() == ["$ abc def echo"], s.screen()
    s.close()


@test("Alt-b/f/d word motion and kill")
def _():
    s = shell()
    s.type("echo aa bb cc")
    s.send("\x1bb\x1bb" + "\x1bd")     # to start of "bb", kill it
    s.send(ENTER)
    expect(s, ["$ echo aa  cc", "aa cc", "$"])
    s.close()


@test("history: Up/Down")
def _():
    s = shell()
    s.type("echo one"); s.send(ENTER)
    s.type("echo two"); s.send(ENTER)
    s.send(UP + UP + ENTER)
    expect(s, ["$ echo one", "one", "$ echo two", "two", "$ echo one", "one", "$"])
    s.send(UP + UP + DOWN + ENTER)       # history is now one, two, one: Up Up Down = the last "one"
    assert s.screen()[-2:] == ["one", "$"], s.screen()
    s.send(UP + UP + UP + ENTER)         # one, two, one, one -> second "two"... counts from the end
    assert s.screen()[-2:] == ["two", "$"], s.screen()
    s.close()


@test("history: Up keeps the line being typed")
def _():
    s = shell()
    s.type("echo one"); s.send(ENTER)
    s.type("echo par"); s.send(UP); s.send(DOWN)
    expect(s, ["$ echo one", "one", "$ echo par"])
    s.close()


@test("history: prefix search (Alt-p)")
def _():
    s = shell()
    s.type("echo one"); s.send(ENTER)
    s.type("ls /"); s.send(ENTER)
    s.type("echo two"); s.send(ENTER)
    s.type("echo "); s.send("\x1bp"); s.send(ENTER)
    assert s.screen()[-2:] == ["two", "$"], s.screen()
    s.close()


@test("history: Ctrl-R")
def _():
    s = shell()
    s.type("echo alpha"); s.send(ENTER)
    s.type("echo beta"); s.send(ENTER)
    s.send(CTRL("R")); s.type("alp")
    assert "(reverse-i-search)`alp': echo alpha" in s.screen()[-1], s.screen()
    s.send(ENTER)
    assert s.screen()[-2:] == ["alpha", "$"], s.screen()
    s.close()


@test("Ctrl-R cancel with Ctrl-G restores the line")
def _():
    s = shell()
    s.type("echo alpha"); s.send(ENTER)
    s.type("xy"); s.send(CTRL("R")); s.type("alp"); s.send(CTRL("G"))
    expect(s, ["$ echo alpha", "alpha", "$ xy"])
    s.close()


@test("history persists in HISTFILE")
def _():
    hf = os.path.join(TMP, "persist")
    if os.path.exists(hf):
        os.unlink(hf)
    s = shell(env={"HISTFILE": hf})
    s.type("echo saved"); s.send(ENTER); s.send(CTRL("D"))
    s.close()
    assert "echo saved" in open(hf).read()
    s = shell(env={"HISTFILE": hf})
    s.send(UP + ENTER)
    assert s.screen()[-2:] == ["saved", "$"], s.screen()
    s.close()


@test("long line wraps; editing across the wrap")
def _():
    s = shell(cols=20)
    s.type("echo " + "a" * 30)
    s.send("\x7f" * 30)               # back to "$ echo "
    expect(s, ["$ echo"])
    s.send("b"); s.send(ENTER)
    expect(s, ["$ echo b", "b", "$"])
    s.close()


@test("exact-width line (pending wrap)")
def _():
    s = shell(cols=10)
    s.type("echo 12345"); s.send("\x7f"); s.send("6"); s.send(ENTER)
    assert s.screen()[-2:] == ["12346", "$"], s.screen()
    s.close()


@test("UTF-8: insert, move, delete whole characters")
def _():
    s = shell()
    s.type("echo éàü")
    s.send(LEFT + "\x7f")
    s.send(ENTER)
    expect(s, ["$ echo éü", "éü", "$"])
    s.close()


@test("wide characters count as two columns")
def _():
    s = shell(cols=12)
    s.type("echo 日本語日本")
    s.send(HOME); s.send(END)
    s.send("\x7f" * 6)
    expect(s, ["$ echo"])
    s.close()


@test("Tab completes a command name")
def _():
    s = shell()
    s.type("ech"); s.send("\t")
    expect(s, ["$ echo"])
    s.close()


@test("Tab completes a path and lists ambiguities")
def _():
    d = os.path.join(TMP, "comp"); os.makedirs(d, exist_ok=True)
    for n in ("alpha1", "alpha2", "beta"):
        open(os.path.join(d, n), "w").close()
    s = shell(cwd=d)
    s.type("echo be"); s.send("\t")
    expect(s, ["$ echo beta"])
    s.send(CTRL("U")); s.type("echo al"); s.send("\t")
    assert s.screen()[-1] == "$ echo alpha", s.screen()
    s.send("\t")
    assert "alpha1" in "\n".join(s.screen()) and "alpha2" in "\n".join(s.screen()), s.screen()
    s.close()


@test("Ctrl-C abandons the line")
def _():
    s = shell()
    s.type("echo nope"); s.send(CTRL("C"))
    expect(s, ["$ echo nope^C", "$"])
    s.type("echo ok"); s.send(ENTER)
    assert s.screen()[-2:] == ["ok", "$"], s.screen()
    s.close()


@test("Ctrl-D on an empty line exits; on text deletes")
def _():
    s = shell()
    s.type("echo ab"); s.send(LEFT + CTRL("D") + ENTER)
    assert s.screen()[-2:] == ["a", "$"], s.screen()
    s.send(CTRL("D"), settle=False); time.sleep(0.3)
    assert not s.alive()
    s.close()


@test("Ctrl-T transposes, Alt-u upcases")
def _():
    s = shell()
    s.type("echo ab"); s.send(CTRL("T")); s.send(ENTER)
    assert s.screen()[-2:] == ["ba", "$"], s.screen()
    s.type("echo word"); s.send("\x1bb\x1bu"); s.send(ENTER)
    assert s.screen()[-2:] == ["WORD", "$"], s.screen()
    s.close()


@test("undo")
def _():
    s = shell()
    s.type("echo abc"); s.send(CTRL("W")); s.send(CTRL("_"))
    expect(s, ["$ echo abc"])
    s.close()


@test("PS1 escapes with \\[ \\] colour codes; cursor column is right")
def _():
    s = shell(env={"PS1": r"\[\e[1;32m\]\u\[\e[0m\]> "}, cols=20)
    s.type("echo " + "x" * 20)
    s.send("\x7f" * 22)
    user = os.environ.get("USER", "user")
    expect(s, ["%s> ech" % user])
    s.close()


@test("multi-line prompt")
def _():
    s = shell(env={"PS1": r"top\n$ "})
    s.type("echo a"); s.send(ENTER)
    expect(s, ["top", "$ echo a", "a", "top", "$"])
    s.close()


@test("bracketed paste of several lines")
def _():
    s = shell()
    s.send("\x1b[200~echo one\necho two\x1b[201~")
    s.send(ENTER)
    assert "one" in s.screen() and "two" in s.screen(), s.screen()
    s.close()


@test("PROMPT_COMMAND runs before each prompt")
def _():
    s = shell(env={"PROMPT_COMMAND": "echo pc"})
    s.type("true"); s.send(ENTER)
    assert s.screen().count("pc") == 2, s.screen()
    s.close()


@test("continuation prompt (PS2)")
def _():
    s = shell(env={"PS2": "> "})
    s.type("echo 'a"); s.send(ENTER)
    s.type("b'"); s.send(ENTER)
    expect(s, ["$ echo 'a", "> b'", "a", "b", "$"])
    s.close()


@test("Ctrl-L clears the screen and redraws the line")
def _():
    s = shell()
    s.type("echo one"); s.send(ENTER)
    s.type("abc"); s.send(CTRL("L"))
    expect(s, ["$ abc"])
    s.close()


@test("startup files: ~/.vashrc, --norc, --rcfile, VASH_RC")
def _():
    home = os.path.join(TMP, "rchome"); os.makedirs(home, exist_ok=True)
    open(os.path.join(home, ".vashrc"), "w").write("PS1='home> '\nalias hi='echo hello'\n")
    s = shell(env={"HOME": home, "PS1": "$ "})
    s.type("hi"); s.send(ENTER)
    expect(s, ["home> hi", "hello", "home>"])
    s.close()
    s = shell(env={"HOME": home}, args=["--norc"])
    expect(s, ["$"])
    s.close()
    other = os.path.join(TMP, "other.rc")
    open(other, "w").write("PS1='other> '\n")
    s = shell(env={"HOME": home}, args=["--rcfile", other])
    expect(s, ["other>"])
    s.close()
    s = shell(env={"HOME": home, "VASH_RC": other})
    expect(s, ["other>"])
    s.close()


@test("bash files are read after ~/.vashrc unless it sets LOAD_BASH_FILES=0")
def _():
    home = os.path.join(TMP, "bashhome"); os.makedirs(home, exist_ok=True)
    open(os.path.join(home, ".bashrc"), "w").write("PS1='bashrc> '\n")
    s = shell(env={"HOME": home})
    expect(s, ["bashrc>"])
    s.close()
    open(os.path.join(home, ".vashrc"), "w").write("FROMVASH=1\n")      # does not stop .bashrc
    s = shell(env={"HOME": home})
    s.type("echo $FROMVASH"); s.send(ENTER)
    expect(s, ["bashrc> echo $FROMVASH", "1", "bashrc>"])
    s.close()
    open(os.path.join(home, ".vashrc"), "w").write("LOAD_BASH_FILES=0\n")
    s = shell(env={"HOME": home})
    expect(s, ["$"])
    s.close()
    open(os.path.join(home, ".bash_profile"), "w").write("PS1='profile> '\n")
    open(os.path.join(home, ".vashrc"), "w").write("")
    s = shell(env={"HOME": home}, args=["--login"])
    time.sleep(0.5)                                  # the machine's own /etc/profile runs first
    assert s.screen()[-1] == "profile>", s.screen()
    s.close()


@test("a broken rc file does not end the session")
def _():
    rc = os.path.join(TMP, "broken.rc")
    open(rc, "w").write("echo before\nif then\nPS1='ok> '\n")
    s = shell(args=["--rcfile", rc])
    s.type("echo alive"); s.send(ENTER)
    assert "alive" in s.screen(), s.screen()
    s.close()


@test("Tab lists every candidate, not just the first 64")
def _():
    d = os.path.join(TMP, "many"); os.makedirs(d, exist_ok=True)
    for i in range(100):
        open(os.path.join(d, "f%03d" % i), "w").close()
    open(os.path.join(d, "fz"), "w").close()
    s = shell(cwd=d, rows=40)
    s.type("echo f"); s.send("\t")
    names = " ".join(s.screen()[1:]).split()
    assert len(names) == 101 and "f000" in names and "fz" in names, names
    assert s.screen()[0] == "$ echo f", s.screen()[0]
    s.close()


@test("menu: Tab and Shift-Tab walk it, Enter takes the choice, Esc drops it")
def _():
    d = os.path.join(TMP, "menu"); os.makedirs(d, exist_ok=True)
    for n in ("m1", "m2", "m3"):
        open(os.path.join(d, n), "w").close()
    s = shell(cwd=d, env={"VASH_AUTOSUGGEST": "0"})
    s.type("echo m"); s.send("\t")
    assert s.screen()[0] == "$ echo m" and s.screen()[1].split() == ["m1", "m2", "m3"], s.screen()
    s.send("\t"); assert s.screen()[0] == "$ echo m1", s.screen()
    s.send("\t"); assert s.screen()[0] == "$ echo m2", s.screen()
    s.send("\x1b[Z"); assert s.screen()[0] == "$ echo m1", s.screen()
    s.send(DOWN); assert s.screen()[0] == "$ echo m2", s.screen()
    s.send(ENTER)                                   # takes m2, does not run the line
    expect(s, ["$ echo m2"])
    s.send(ENTER)
    expect(s, ["$ echo m2", "m2", "$"])
    s.type("echo m"); s.send("\t"); s.send("\t"); s.send("\x1b")
    expect(s, ["$ echo m2", "m2", "$ echo m"])
    s.close()


@test("Tab on a glob word cycles through its matches, like fish")
def _():
    d = os.path.join(TMP, "globtab"); os.makedirs(d, exist_ok=True)
    for n in ("a.sh", "b.sh", "c.txt", "only.md"):
        open(os.path.join(d, n), "w").close()
    s = shell(cwd=d, env={"VASH_AUTOSUGGEST": "0"})
    s.type("rm *.sh"); s.send("\t")
    assert s.screen()[0] == "$ rm *.sh" and s.screen()[1].split() == ["a.sh", "b.sh"], s.screen()
    s.send("\t"); assert s.screen()[0] == "$ rm a.sh", s.screen()
    s.send("\t"); assert s.screen()[0] == "$ rm b.sh", s.screen()
    s.send(ENTER)
    s.send(CTRL("U")); s.type("echo *.md"); s.send("\t")        # one match: replaced at once
    expect(s, ["$ echo only.md"])
    s.close()


@test("menu scrolls when it does not fit")
def _():
    d = os.path.join(TMP, "lots"); os.makedirs(d, exist_ok=True)
    for i in range(150):
        open(os.path.join(d, "g%03d" % i), "w").close()
    s = shell(cwd=d, rows=10)
    s.type("echo g"); s.send("\t")
    assert any("rows 1-5 of 12" in l for l in s.screen()), s.screen()
    for _ in range(80):
        s.send("\t", settle=False)
    s.settle()
    assert s.screen()[0].startswith("$ echo g0") and any("of 12" in l for l in s.screen()), s.screen()
    s.close()


@test("command completion sees a program added after the first Tab")
def _():
    bindir = os.path.join(TMP, "pbin"); os.makedirs(bindir, exist_ok=True)
    s = shell(env={"PATH": bindir + ":" + os.environ["PATH"]})
    s.type("zzprog"); s.send("\t")
    expect(s, ["$ zzprog"])                       # nothing yet: the cache is built
    f = os.path.join(bindir, "zzprogram")
    open(f, "w").close(); os.chmod(f, 0o755)
    s.send("\t")
    expect(s, ["$ zzprogram"])
    s.close()


@test("complete -W and complete -F drive Tab")
def _():
    rc = os.path.join(TMP, "spec.rc")
    open(rc, "w").write("complete -W 'alpha beta bravo' mycmd\n"
                        "_f() { COMPREPLY=($(compgen -W 'one two' -- \"${COMP_WORDS[COMP_CWORD]}\")); }\n"
                        "complete -F _f fcmd\n")
    s = shell(args=["--rcfile", rc])
    s.type("mycmd be"); s.send("\t")
    expect(s, ["$ mycmd beta"])
    s.send(CTRL("U")); s.type("mycmd b"); s.send("\t")
    assert s.screen()[0] == "$ mycmd b" and s.screen()[1].split() == ["beta", "bravo"], s.screen()
    s.send(CTRL("U")); s.type("echo x; fcmd tw"); s.send("\t")
    assert s.screen()[-1] == "$ echo x; fcmd two", s.screen()
    s.close()


@test("specs load lazily from VASH_COMPLETION_PATH; cd completes directories only")
def _():
    d = os.path.join(TMP, "specs"); os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "comptest.vash"), "w").write("complete -W 'red green' comptest\n")
    w = os.path.join(TMP, "work"); os.makedirs(os.path.join(w, "namedir"), exist_ok=True)
    open(os.path.join(w, "namefile"), "w").close()
    s = shell(env={"VASH_COMPLETION_PATH": d}, cwd=w)
    s.type("comptest g"); s.send("\t")
    expect(s, ["$ comptest green"])
    s.send(CTRL("U")); s.type("cd na"); s.send("\t")
    expect(s, ["$ cd namedir/"])
    s.close()


@test("autosuggestion: shown after the cursor, taken with Right/End, never run unasked")
def _():
    s = shell()
    s.type("echo alphabet"); s.send(ENTER)
    s.type("echo al")
    expect(s, ["$ echo alphabet", "alphabet", "$ echo alphabet"])      # grey rest is on screen
    s.send(RIGHT); s.send(ENTER)
    assert s.screen()[-2:] == ["alphabet", "$"], s.screen()
    s.type("echo al"); s.send(ENTER)                                     # Enter runs only what was typed
    assert s.screen()[-3:] == ["$ echo al", "al", "$"], s.screen()
    s.type("echo a"); s.send(END); s.send(ENTER)                         # the newest match is "echo al"
    assert s.screen()[-2:] == ["al", "$"], s.screen()
    s.close()
    s = shell(env={"VASH_AUTOSUGGEST": "0"})
    s.type("echo one"); s.send(ENTER); s.type("echo o")
    assert s.screen()[-1] == "$ echo o", s.screen()
    s.close()


@test("highlighting: command words are green when they run something, red when not")
def _():
    s = shell()
    s.raw = b""
    s.type("echo hi | zzznotacmd x; FOO=1 ls")
    raw = s.raw.decode()
    assert "\x1b[32mecho\x1b[0m" in raw, raw[-200:]
    assert "\x1b[31mzzznotacmd\x1b[0m" in raw, raw[-200:]
    assert "FOO=1 \x1b[32mls" in raw, raw[-200:]
    expect(s, ["$ echo hi | zzznotacmd x; FOO=1 ls"])
    s.close()
    s = shell(env={"VASH_HIGHLIGHT": "0"})
    s.raw = b""
    s.type("echo hi")
    assert "\x1b[32m" not in s.raw.decode()
    s.close()


@test("fish-style complete: options with descriptions, conditions, exclusive")
def _():
    d = os.path.join(TMP, "fishspecs"); os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "fcmd.vash"), "w").write(
        "complete -c fcmd -s h -l help -d 'Show help'\n"
        "complete -c fcmd -l verbose -d 'More output'\n"
        "complete -c fcmd -n '[[ ${#COMP_WORDS[@]} -le 2 ]]' -a 'add commit' -d 'a subcommand'\n"
        "complete -c fcmd -n '[[ ${COMP_WORDS[1]} == add ]]' -x -a 'staged unstaged'\n")
    work = os.path.join(TMP, "fishwork"); os.makedirs(work, exist_ok=True)
    open(os.path.join(work, "sa-file"), "w").close()
    s = shell(env={"VASH_COMPLETION_PATH": d, "VASH_AUTOSUGGEST": "0"}, cwd=work)
    s.type("fcmd -"); s.send("\t")
    scr = s.screen()
    assert scr[0] == "$ fcmd -" and any("--help" in l and "Show help" in l for l in scr) \
        and any("--verbose" in l and "More output" in l for l in scr), scr
    s.send(CTRL("U")); s.type("fcmd a"); s.send("\t")
    expect(s, ["$ fcmd add"])
    s.type("s"); s.send("\t")                                 # -x: "sa-file" is not offered
    expect(s, ["$ fcmd add staged"])
    s.close()


@test("autosuggestion: Alt-F takes one word")
def _():
    s = shell()
    s.type("echo one two three"); s.send(ENTER)
    s.type("echo"); s.send("\x1bf"); s.send(ENTER)
    assert s.screen()[-2:] == ["one", "$"], s.screen()
    s.close()


@test("Tab completes ~/ paths")
def _():
    home = os.path.join(TMP, "tildehome"); os.makedirs(os.path.join(home, "Documents"), exist_ok=True)
    s = shell(env={"HOME": home})
    s.type("cd ~/Doc"); s.send("\t")
    expect(s, ["$ cd ~/Documents/"])
    s.send(CTRL("U")); s.type("echo ~"); s.send("\t")           # ~ itself and the users: a menu
    scr = " ".join(s.screen()[1:]).split()
    assert s.screen()[0] == "$ echo ~" and "~" in scr and "~root" in scr, s.screen()
    s.close()


@test("generated .vash specs of the tbx commands (ls -<Tab>), files otherwise")
def _():
    d = os.path.abspath(os.path.join(os.path.dirname(VS), "..", "..", "vaporOS-coreutils", "completions"))
    if not os.path.isfile(os.path.join(d, "ls.vash")):
        return                                           # the sibling repo is not here
    w = os.path.join(TMP, "optwork"); os.makedirs(w, exist_ok=True)
    open(os.path.join(w, "afile"), "w").close()
    s = shell(cwd=w, env={"VASH_AUTOSUGGEST": "0", "VASH_COMPLETION_PATH": d})
    s.type("ls --co"); s.send("\t")                     # a single match completes
    expect(s, ["$ ls --color"])
    s.send(CTRL("U")); s.type("ls -"); s.send("\t")
    scr = s.screen()
    assert any("-1" in l and "list one file per line" in l for l in scr), scr
    s.send(CTRL("U")); s.type("ls af"); s.send("\t")
    expect(s, ["$ ls afile"])
    s.close()


@test("a completion function or condition that never ends is stopped")
def _():
    rc = os.path.join(TMP, "slow.rc")
    open(rc, "w").write("_slow() { while true; do :; done; }\n"
                        "complete -F _slow slowcmd\n"
                        "complete -c slow2 -n 'while true; do :; done' -a never\n")
    s = shell(args=["--rcfile", rc], env={"VASH_COMPLETION_TIMEOUT": "200"})
    for line in ("slowcmd x", "slow2 x"):
        s.type(line); s.send("\t")
        s.send("Z")                                     # the prompt answered: the loop was cut off
        assert s.screen()[-1] == "$ " + line + "Z", s.screen()
        s.send(CTRL("U"))
    s.close()


@test("bind: a command, shell code (-x), -r, -p, -l")
def _():
    rc = os.path.join(TMP, "bind.rc")
    open(rc, "w").write("bind '\"\\C-o\": kill-line'\n"
                        "bind -x '\"\\C-g\": READLINE_LINE=\"echo $READLINE_LINE!\"; READLINE_POINT=${#READLINE_LINE}'\n"
                        "bind '\"\\e[1;5A\": beginning-of-line'\n")
    s = shell(args=["--rcfile", rc], env={"VASH_AUTOSUGGEST": "0"})
    s.type("abc"); s.send(CTRL("A")); s.send(CTRL("O"))          # Ctrl-O is kill-line now
    expect(s, ["$"])
    s.type("hi"); s.send(CTRL("G"))                               # the shell code rewrote the line
    assert s.screen()[-1] == "$ echo hi!", s.screen()
    s.send(CTRL("U")); s.type("xy"); s.send("\x1b[1;5A"); s.type("Q")
    assert s.screen()[-1] == "$ Qxy", s.screen()
    s.send(END); s.send(CTRL("U")); s.type("bind -r '\\C-o'; bind -p | head -3"); s.send(ENTER)
    assert any(l.startswith('"\\C-g"') for l in s.screen()) or any('"\\e[1;5A": beginning-of-line' in l for l in s.screen()), s.screen()
    s.type("bind -l | head -2"); s.send(ENTER)
    assert "accept-line" in s.screen() or "backward-char" in " ".join(s.screen()), s.screen()
    s.close()


@test("looser matches: other case, then containing the word; they replace what was typed")
def _():
    d = os.path.join(TMP, "loose"); os.makedirs(os.path.join(d, "Documents"), exist_ok=True)
    for n in ("my-report-final.txt", "alpha-test", "beta-test", "prog"):
        open(os.path.join(d, n), "w").close()
    os.chmod(os.path.join(d, "prog"), 0o755)
    s = shell(cwd=d, env={"VASH_AUTOSUGGEST": "0"})
    s.type("cd doc"); s.send("\t")
    expect(s, ["$ cd Documents/"])
    s.send(CTRL("U")); s.type("cat port"); s.send("\t")
    expect(s, ["$ cat my-report-final.txt"])
    s.send(CTRL("U")); s.type("ls test"); s.send("\t")                 # two matches: a menu, line untouched
    assert s.screen()[0] == "$ ls test" and s.screen()[1].split() == ["alpha-test", "beta-test"], s.screen()
    s.send("\t"); assert s.screen()[0] == "$ ls alpha-test", s.screen()
    s.send("\t"); assert s.screen()[0] == "$ ls beta-test", s.screen()
    s.send("\x1b"); assert s.screen()[0] == "$ ls test", s.screen()      # Esc puts the typed word back
    s.send(CTRL("U")); s.type("ECH"); s.send("\t")
    expect(s, ["$ echo"])
    s.send(CTRL("U")); s.type("./pr"); s.send("\t")                    # path in command position
    expect(s, ["$ ./prog"])
    s.close()


@test("highlighting: quotes are yellow, existing paths are underlined")
def _():
    d = os.path.join(TMP, "hlpaths"); os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "realfile"), "w").close()
    s = shell(cwd=d, env={"VASH_AUTOSUGGEST": "0"})
    s.raw = b""
    s.type("echo 'hi there' realfile nofile \"q\" -n")
    raw = s.raw.decode().split("\x1b[J")[-1]               # the last redraw
    assert "\x1b[33m'hi there'\x1b[0m" in raw, repr(raw)
    assert "\x1b[4mrealfile\x1b[0m" in raw, repr(raw)
    assert "nofile \x1b[33m\"q\"\x1b[0m -n" in raw, repr(raw)   # missing file, plain; option not checked
    s.send(CTRL("U")); s.raw = b""
    s.type("echo 'open")
    assert s.raw.decode().split("\x1b[J")[-1].endswith("\x1b[33m'open\x1b[0m"), repr(s.raw[-80:])
    s.close()


@test("!! !$ !n !prefix ^old^new and fc -l / -s")
def _():
    s = shell(env={"VASH_AUTOSUGGEST": "0", "VASH_HIGHLIGHT": "0"})
    s.type("echo alpha beta"); s.send(ENTER)
    s.type("echo !$ !!"); s.send(ENTER)                 # beta, then the whole previous command
    s.type("^beta^gamma"); s.send(ENTER)
    s.type("!echo"); s.send(ENTER)
    s.type("echo '!!' \\!!"); s.send(ENTER)               # quoted and escaped: left alone
    s.type("!nosuch"); s.send(ENTER)
    s.type("fc -s alpha=omega 'echo alpha'"); s.send(ENTER)
    s.type("fc -ln 1 2"); s.send(ENTER)
    scr = "\n".join(s.screen())
    for want in ("echo beta echo alpha beta", "echo gamma echo alpha beta", "!nosuch: event not found",
                 "echo omega beta", "!! !!"):
        assert want in scr, (want, scr)
    s.type("set +H"); s.send(ENTER)
    s.type("echo !!"); s.send(ENTER)
    assert s.screen()[-2].endswith("echo !!") or "!!" in s.screen()[-2], s.screen()
    s.close()


@test("~/.inputrc is read at startup")
def _():
    d = os.path.join(TMP, "rc"); os.makedirs(d, exist_ok=True)
    open(os.path.join(d, ".inputrc"), "w").write('# a comment\nset editing-mode emacs\n"\\C-o": backward-kill-line\n"\\C-t": no-such-readline-fn\n')
    s = shell(env={"HOME": d, "VASH_AUTOSUGGEST": "0"})
    s.type("echo gone"); s.send(CTRL("O")); s.type("echo ok"); s.send(ENTER)
    scr = "\n".join(s.screen())
    assert "unknown function" not in scr and "$ echo gone" not in scr and "\nok" in "\n" + scr, scr
    s.close()


@test("kill -<Tab> offers signals; a # comment is grey")
def _():
    s = shell(env={"VASH_AUTOSUGGEST": "0"})
    s.type("kill -TER"); s.send("\t")
    expect(s, ["$ kill -TERM"])
    s.send(CTRL("U")); s.raw = b""
    s.type("echo a #why")
    assert "\x1b[90m#why\x1b[0m" in s.raw.decode().split("\x1b[J")[-1], repr(s.raw[-60:])
    s.close()


@test("an optional spec file completes git subcommands")
def _():
    s = shell(env={"VASH_COMPLETION_PATH": os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "completions"), "VASH_AUTOSUGGEST": "0"})
    s.type("git sta"); s.send("\t")
    assert "stash" in s.screen()[-1] and "status" in s.screen()[-1], s.screen()
    s.close()


@test("Tab leaves $? alone, even when it loads a spec file")
def _():
    d = os.path.join(TMP, "statspec"); os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "stcmd.vash"), "w").write("complete -c stcmd -s h\n")
    s = shell(env={"VASH_COMPLETION_PATH": d})
    s.type("false"); s.send(ENTER)
    s.type("stcmd -"); s.send("\t"); s.send(CTRL("U"))
    s.type('echo "rc=$?"'); s.send(ENTER)
    assert "rc=1" in s.screen(), s.screen()
    s.close()


@test("aliases and functions complete in command position")
def _():
    s = shell(env={"VASH_AUTOSUGGEST": "0"})
    s.type("alias zzalias='echo a'"); s.send(ENTER)
    s.type("zzfunc() { :; }"); s.send(ENTER)
    s.type("zzal"); s.send("\t"); expect(s, ["$ alias zzalias='echo a'", "$ zzfunc() { :; }", "$ zzalias"])
    s.send(CTRL("U")); s.type("zzfu"); s.send("\t")
    assert s.screen()[-1] == "$ zzfunc", s.screen()
    s.close()


@test("a SIGTERM while typing leaves the terminal as it was")
def _():
    import signal, termios
    s = shell()
    s.type("abc")
    os.kill(s.pid, signal.SIGTERM); time.sleep(0.3)
    lflag = termios.tcgetattr(s.fd)[3]
    assert lflag & termios.ECHO and lflag & termios.ICANON, lflag
    s.close()


@test("history is saved as each command is entered, not at exit")
def _():
    hf = os.path.join(TMP, "eachcmd")
    if os.path.exists(hf):
        os.unlink(hf)
    s = shell(env={"HISTFILE": hf})
    s.type("echo kept"); s.send(ENTER)
    s.close()                                        # killed: no clean exit
    assert "echo kept" in open(hf).read()


@test("Tab completes jobs, ~user and CDPATH directories")
def _():
    base = os.path.join(TMP, "cdp"); os.makedirs(os.path.join(base, "projectdir"), exist_ok=True)
    s = shell(env={"CDPATH": base, "VASH_AUTOSUGGEST": "0"})
    s.type("sleep 30 &"); s.send(ENTER)
    s.type("fg "); s.send("\t")
    assert s.screen()[-1] == "$ fg %1", s.screen()
    s.send(CTRL("U")); s.type("kill %"); s.send("\t")
    assert s.screen()[-1] == "$ kill %1", s.screen()
    s.send(CTRL("U")); s.type("echo ~roo"); s.send("\t")
    assert s.screen()[-1] == "$ echo ~root/", s.screen()
    s.send(CTRL("U")); s.type("cd projectd"); s.send("\t")
    assert s.screen()[-1].startswith("$ cd projectdir"), s.screen()
    s.send(CTRL("U")); s.type("kill %1"); s.send(ENTER)
    s.close()


@test("Tab completes variable names")
def _():
    s = shell()
    s.type("VSTESTVAR=1"); s.send(ENTER)
    s.type("echo $VSTEST"); s.send("\t")
    expect(s, ["$ VSTESTVAR=1", "$ echo $VSTESTVAR"])
    s.close()


@test("Ctrl-C at a continuation prompt drops the command; Ctrl-D reports it")
def _():
    s = shell()
    s.type("echo 'a"); s.send(ENTER); s.send(CTRL("C"))
    s.type("echo ok"); s.send(ENTER)
    expect(s, ["$ echo 'a", "> ^C", "$ echo ok", "ok", "$"])
    s.type("echo 'b"); s.send(ENTER); s.send(CTRL("D"))
    assert any("unexpected end of file" in l for l in s.screen()), s.screen()
    s.close()


@test("HISTFILE is private and trimmed to HISTSIZE")
def _():
    hf = os.path.join(TMP, "private")
    if os.path.exists(hf):
        os.unlink(hf)
    for n in range(4):
        s = shell(env={"HISTFILE": hf, "HISTSIZE": "2"})
        s.type("echo %d" % n); s.send(ENTER); s.send(CTRL("D"), settle=False)
        time.sleep(0.3); s.close()
    assert oct(os.stat(hf).st_mode & 0o777) == "0o600", oct(os.stat(hf).st_mode)
    assert len(open(hf).read().splitlines()) <= 3, open(hf).read()


@test("HISTCONTROL=ignoreboth skips leading-space and repeated lines")
def _():
    s = shell(env={"HISTCONTROL": "ignoreboth"})
    for line in ("echo a", "echo a", " echo hidden", "history"):
        s.type(line); s.send(ENTER)
    out = [l for l in s.screen() if l.startswith("    ")]
    assert [l.split(None, 1)[1] for l in out] == ["echo a", "history"], out
    s.close()


def main():
    global VS
    VS = os.path.abspath(sys.argv[1])
    only = sys.argv[2] if len(sys.argv) > 2 else ""
    ok = bad = 0
    failed = []
    for fn in results:
        if only and only not in fn.test_name:
            continue
        try:
            fn()
            ok += 1
            print("ok      %s" % fn.test_name)
        except Exception as ex:  # noqa: BLE001
            bad += 1
            failed.append(fn.test_name)
            print("FAILED  %s\n    %s" % (fn.test_name, str(ex).replace("\n", "\n    ")))
    print("pty: %d passed, %d failed" % (ok, bad))
    tally = os.environ.get("VASH_TALLY")
    if tally and not only:
        with open(tally, "a") as t:
            for name in failed:
                t.write("F\t%s\t%s\n" % ("line editor (pty)", name))
            t.write("G\t%s\t%d\t%d\t0\n" % (os.environ.get("VASH_PLATFORM", "Linux") + ": line editor (pty)", ok, bad))
    sys.exit(1 if bad else 0)


main()
