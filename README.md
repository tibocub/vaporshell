# vaporshell

> The shell of [vaporOS](https://github.com/tibocub/vaporOS-nuttx) (also support linux/mac/bsd)


**\/!\\ Currently focusing on bash/posix-compatibilty, portability, speed and memory efficiency. NOT safety (+ I'm not a cybersecurity expert anyway) \/!\\**


## Readme/codebase TODOs
- make some benchmarks to show how freakin fast and memory efficient this is compared to bash and fish
- make the README more concise (migrate some info to the docs)
- make a config menu (with dialog ?) for easy custom compilation, change target, select features, etc.

## TLDR

Vaporshell is a lightweight and protable shell that aims to provide:
- Bash compatiblity by default
- POSIX sh compatibility (like bash does, with a "--posix" mode)
- A fast and comfortable user experience inspired by fish (for instance, our autocompletion engine is much faster AND more advanced than bash's)

All this while being small enought to run on an RP2040 or an ESP32 (using NuttX/VaporOS)


## Support and compatibility

bash mode tested agains **bash 5.3.9** (see [bash-coverage.md](docs/bash-coverage.md))
POSIX mode tested agains **dash 0.5.12** (see [posix-coverage.md](docs/posix-coverage.md))

Tested on linux (fedora 44 and debian 13) and
vaporOS (RP2040 and native sim).

Not tested yet but support is planned for BSDs
and MacOS as well.


## GOALS

- A real POSIX shell first: tokenizing, quoting, `$VAR` expansion,
  redirection, control flow (`if`/`for`/`while`), functions -- the
  actual `sh` language, not a look-alike. This is the priority; a
  correct, if incomplete, POSIX shell beats a shell that half-does
  POSIX and half-does something else.
- Small and fast ("bash, but fast"). Every interactive feature's size
  cost is measured (no compile-time switches: what only some users
  need is an optional `.vsh` file); completion is native code plus
  declarative, lazily loaded specs rather than shell functions that fork
  helpers. See `docs/interactive.md`.
- Once that's solid: steal the bash/fish quality-of-life features that
  make an interactive shell pleasant to actually use day to day --
  tab completion, history, `.bashrc`-style startup, sensible line
  editing. Not aiming for 100% bash compatibility, ever -- just the
  parts that make vaporshell nicer to type into, without compromising
  POSIX `sh` as the thing scripts can actually rely on.
- Independent of any specific command set. vaporshell doesn't know
  what `ls` or `poweroff` are, or where they come from -- it resolves
  `$PATH` and runs whatever's there, the same way any real Unix shell
  does. [vaporOS-coreutils](https://github.com/tibocub/vaporOS-coreutils)
  is what vaporOS itself ships as that command set, but nothing here
  is coupled to it specifically, and other NuttX projects could use
  vaporshell with a completely different set of programs.


## LAYOUT

[#layout](#layout)

```
vaporshell/
  vaporshell_main.c   entry point, interactive loop, -c / script / stdin
  lexer.c parser.c    text -> tokens -> AST (POSIX grammar); parse.h
  wordscan.c          where quotes / ${} / $() end -- shared by lexer + expander
  ast.h arena.c       the AST, and the arena it lives in
  expand.c            word expansion (fields, IFS splitting, params, $(...))
  arith.c             $(( )) evaluation
  glob.c              patterns: globbing, case, ${x#pat}
  exec.c redir.c      run an AST; redirections; command substitution
  vars.c              variable table, positional parameters, functions
  mode.c              language modes (bash / POSIX) as feature bits; mode.h
  builtins.c test.c   the builtin table; test / [
  traps.c             trap, kill
  help.c              help (reads the builtin table)
  util.c              allocation, string buffers, errors
  prompt.c            PS1/PS2/PS4 escapes, PROMPT_COMMAND
  ledit.c history.c   the line editor and history (own code, both platforms)
  compgen.c compfish.c  compgen/complete builtins, fish-style specs
  bind.c rcfile.c     key bindings (bind, inputrc), startup files
  complete.c          tab completion candidates
  platform.h          what differs between NuttX and a host OS
  platform_nuttx.c    NuttX: no fork, posix_spawnp, tbx fallback (dispatch.c)
  posix/              standalone build only: platform.c (fork, PATH, spawn)
                      and stand-in <nuttx/...> headers
  posix.mk            standalone build (see BUILD)
  Kconfig, Makefile   standard NuttX app-directory shape
  completions/        optional completion specs (git, ssh) for $VS_COMPLETION_PATH
  docs/interactive.md the interactive-shell plan + status (prompt, editor, history, completion, rc files)
  docs/design.md      the design doc -- read this first for anything non-trivial
  tests/              own/ (vs bash and dash), modes/ (per-mode suites),
                      run-suites.sh (all suites), check-all.sh (everything,
                      one report), nuttx-check.sh (build + test on vaporOS),
                      smoosh corpus + smoosh-check.sh,
                      coverage/ (probe tool, doc generator),
                      reference/run-differential.py (also drives NuttX),
                      nuttx-sim-smoke.py, nuttx-symcheck.sh
  docs/               design.md, modes.md, and the generated
                      bash-coverage.md / posix-coverage.md
```

Symlinked into [vaporOS-nuttx](https://github.com/tibocub/vaporOS-nuttx) as `vaporshell/`, the same pattern `vaporOS-coreutils` uses for `toybox/` -- see that repo's own `setup.sh` for how it gets cloned and
wired in automatically.


## BUILD

[#build](#build)

NuttX: unchanged -- the `Makefile` is NuttX's app-directory shape and is
used whenever `APPDIR` is set. Every root `.c` file must be listed there
(`platform_nuttx.c` and `dispatch.c` are NuttX-only). After a build:

```
make -f posix.mk check-vaporos     # rebuild what changed, then test in the simulator
make -f posix.mk check-vaporos FULL=1   # the full vaporOS build first (minutes)
```

`tests/nuttx-check.sh` does the work: it rebuilds (incrementally by default),
then runs the global-name collision check, the simulator smoke test and every
differential suite, the same ones the Linux build runs. It expects the
vaporOS workspace layout (`../vaporOS`, `../nuttx`; override with
`VAPOROS_DIR` / `NUTTX_DIR`). The pieces also run on their own:

```
sh tests/nuttx-symcheck.sh ../nuttx/staging      # global-name collisions
python3 tests/nuttx-sim-smoke.py ../nuttx/nuttx  # run it in the simulator
python3 tests/reference/run-differential.py --all-suites --brief --nuttx-dir ../nuttx
```

Test a change everywhere before pushing with `make -f posix.mk check-all`
(Linux, Linux under ASan/UBSan, smoosh, vaporOS): it keeps going after a
failure and ends with a single `TOTAL: N passed, M failed` line. Test files
can declare what NuttX cannot do with a `# requires: ...` line (see
`NUTTX_LACKS` in `run-differential.py`); those are reported as skipped.

Everything in `libapps.a` shares one flat symbol namespace, so give any new
global a `vs_`/`g_vs_` style name; `symcheck` finds the ones that clash
(the first version collided with NuttX's own `g_builtins`).

Standalone (Linux, macOS, BSD), from a plain checkout:

```
make                        # build vaporshell
make check                  # every suite, each vs its reference shell
make check ONLY=printf      # only tests whose name contains "printf"
make help                   # every target
                            # (BASH_REF=/path/to/bash selects the bash reference)
make coverage               # regenerate docs/*-coverage.md from the probes
vaporshell --posix          # POSIX mode (also -o posix, or invoked as sh)
make check-smoosh           # the smoosh corpus in --posix mode, regressions named
make check-asan             # same tests under ASan + UBSan
make check-pty              # the line editor in a pseudo-terminal (tests/pty)
make bench                  # Tab-completion timings (tests/bench)
make strict                 # fortify off, -Werror (what Fedora sees)
```


## STATUS

[#status](#status)

The front end is a real lexer + parser producing an AST, and an executor
that walks it -- not text rescanning. Words keep their quoting until
expansion, so one word can become many fields (IFS splitting, `"$@"`,
globbing). Variables live in a table, not in `environ`: only exported ones
reach a child. See `docs/design.md`, "Architecture".

Modes: bash's behaviour is the default and `--posix` selects POSIX; both are
presets of feature bits over one engine (`mode.h`, `docs/modes.md`). Each bit
is a measured difference with a test.

Working: the scripting language, essentially in full -- quoting, all POSIX
and bash expansions (parameter operators including substring/replace/case
conversion, `$(...)`, backticks, `$(( ))`, tilde, brace expansion, field
splitting, pathname expansion, process substitution), all POSIX and bash
redirections and here-documents/here-strings, pipelines, `&&`/`||`/`!`,
`;`/`&`, `if`/`for`/`while`/`until`/`case`/`select`, the C-style
`for ((;;))`, `[[ ]]` and `(( ))`, `{ }`, `( )`, functions (both forms),
`local` (including namerefs), indexed and associative arrays,
`break`/`continue N`/`return`, `set -e -u -x -f -C -a -n -v -o pipefail`,
`shopt`, `trap` (EXIT and signals), aliases, job control (`jobs`/`fg`/`bg`/
`wait`/`disown`, no real terminal job control), `coproc`, `declare`/
`typeset`, `mapfile`, `enable`/`compgen`, `$LINENO`, and the builtins in
`help`. See the TODO list below for the itemised, honest version, including
what's *not* done. The interactive layer (own line editor, prompt escapes,
history with `!` expansion and `fc`, rc files and `~/.inputrc`, `bind`, menu
completion with specs, autosuggestions, syntax highlighting) exists on Linux
and vaporOS, including the RP2040; its status table, design and measured
sizes and latencies are in [docs/interactive.md](docs/interactive.md).

How close each mode is to its reference is measured, not claimed: see
[docs/bash-coverage.md](docs/bash-coverage.md) (bash 5.3.0) and
[docs/posix-coverage.md](docs/posix-coverage.md) (dash 0.5.12), regenerated
with `python3 tests/coverage/gen-docs.py build/vaporshell --bash /path/to/bash`.

Smoosh corpus (`make check-smoosh`, run in `--posix` mode): 146 of 184 by a
rough pass rule (see `tests/smoosh-check.sh`); dash scores 147 and
`bash --posix` 143 by the same rule. (Two tests call bash's `source`, which
POSIX mode deliberately does not have.) The rest are listed in `tests/smoosh-known-failures.txt`.

Platform notes: on the standalone build `( )`, `&`, pipelines with
builtins/functions, and `$(...)` fork. NuttX has no `fork()`, so there they
run **in-process** (`inproc.c`): the shell's state is snapshotted, the body
runs, and the snapshot is restored, so subshells still cannot change the
parent. `$(...)` output and in-process pipeline stages are handed over by
small helper threads (NuttX pipes hold only 1 KiB), so functions and
variables are visible inside `$(...)` and builtins work in pipelines. What
differs from a real subshell: it is not concurrent (an endless in-process
producer piped into `head` never ends), `&` only works for external
programs, and nested `$(...)` uses the shell's own task stack (raise the
stack size for deeply nested scripts). This needs pthreads. `VS_INPROC=1` runs
the same code on a host, which `make check` uses to test it against bash and
dash.

NuttX: built and run in the simulator against `releases/13.0` with the
vaporOS `sim:nsh` configuration (`tests/nuttx-sim-smoke.py` drives it).
Also run on a real RP2040 (Waveshare RP2040-Zero) over USB, including the line editor; latency and memory are measured in `docs/interactive.md`.

Also worth knowing: the line editor has no length limit of its own, but
when input is not a terminal (or the editor cannot start) lines are read with
a plain reader; script files and `-c` strings have no such limit either.


## TODO

Goal for this list specifically: comprehensive enough that filling
every box would mean vaporshell is very close to fully bash-compatible
-- a reference/roadmap, not a near-term commitment. The project's own
stated priority (see GOALS above) stays POSIX-first, bash
quality-of-life second; this list exists so nothing bash actually does
is invisible while that priority order gets worked through. Updated
whenever something gets implemented -- if this drifts from reality,
treat that as a bug in the list, not in the code. As of the `v0.1.0`
tag, the scripting language itself (everything below except the
"Interactive quality-of-life" section) is done to the point that how
close each mode is to its reference is a measured number, not a guess:
see [docs/bash-coverage.md](docs/bash-coverage.md) and
[docs/posix-coverage.md](docs/posix-coverage.md).


### Basic shell features
- [x] execute a script file
- [x] source a script into the current session (`.` and `source`)
- [x] variables (`num=3; echo $num`)
- [x] `;` end line with semicolon
- [x] quoted assignment values (`age=5; name="tibo"`)
- [x] `$VAR` / `${VAR}` expansion
- [x] command substitution: `$(...)` and `` `...` ``
- [x] `&&` / `||` conditional execution
- [x] backslash escaping (`\`)
- [x] pipes (`|`, distinct from `||`) -- real multi-stage pipeline
      execution with genuine pipe-based data flow between commands;
      required enabling `CONFIG_SCHED_CHILD_STATUS` in vaporOS-nuttx's
      own build.sh -- without it, NuttX's own waitpid() can't retrieve
      a fast-exiting command's status if it already exited before
      waitpid() got called (confirmed directly in NuttX's own Kconfig
      help text for that option), which pipelines hit routinely since
      every stage has to be spawned before any of them are waited on
- [x] redirection: `>`, `<`, `>>`, `2>`, `2>&1`, `&>`, `&>>`, `n>&m`
- [x] here-documents (`<<`, `<<-`) and here-strings (`<<<`)
- [x] process substitution (`<(...)`, `>(...)`) -- materialized as a
      real temp file rather than a true concurrent pipe, since this
      shell has no background-execution model to build that on
      (NuttX has no `fork()`); correct end results for the dominant
      real-world usage (`diff <(...) <(...)`, `tee >(...)`), not true
      concurrency -- see `docs/bash-coverage.md`
- [x] subshells: `( cmd1; cmd2 )` in an isolated child environment
- [x] command grouping: `{ cmd1; cmd2; }` in the *current* environment
      (distinct from subshells -- no isolation, just sequencing)
- [x] globbing / wildcard expansion (`*`, `?`, `[...]`)
- [x] brace expansion (`{a,b,c}`, `{1..5}`, `{1..10..3}`)
- [x] tilde expansion (`~`, `~user`)
- [x] background execution (`&`), `jobs`/`fg`/`bg`/`wait`/`disown`,
      and `kill %N` -- `jobs` prints bash's exact column format
      (status text, `[N]+`/`[N]-` markers); job ids count up forever
      rather than being reused, unlike bash's. No *real* terminal job
      control (process groups, `Ctrl-Z` suspension, `tcsetpgrp`) --
      NuttX itself only stubs process groups (see
      `docs/c-posix-compatibility.md` in vaporOS-nuttx) -- so `fg`/`bg`
      work with what a job can actually be here: running in the
      background, or finished
- [x] `coproc [NAME] command` -- the array `NAME`/`NAME_PID`; a plain
      external command works everywhere (no fork needed, like `&`
      itself); a compound-command body needs a real fork, so it's
      host-only there


### Control flow
- [x] `if`/`then`/`elif`/`else`/`fi`
- [x] `for NAME in LIST; do ...; done`, and bare `for NAME; do ...`
      (positional parameters)
- [x] `for ((init; cond; step)); do ...; done` (C-style, bash-specific)
- [x] `while ...; do ...; done`
- [x] `until ...; do ...; done`
- [x] `case ... in ... esac` -- literal text, `*`, `?`, `|`
      alternation, and bracket expressions (`[abc]`/`[a-z]`);
      `;&`/`;;&` fallthrough
- [x] `select ...; do ...; done` (bash-specific menu construct) --
      column layout matches bash's exactly, including the quirk where
      a layout that would end up one row wide falls back to one item
      per line
- [x] `break` / `continue` (including `break N` / `continue N`)
- [x] real `[[ ... ]]` semantics -- its own grammar (no word splitting
      or globbing inside, unquoted `<`/`>`, `=~` with `BASH_REMATCH`)


### Functions
- [x] `name() { ...; }` and the bash `function name { ...; }` form
- [x] `local` (function-scoped variables), including `local -n`/`-i`/etc.
- [x] `return`
- [x] recursion


### Special variables / positional parameters
- [x] `$0` (script/shell name), `$1`-`$9`, `${10}`+ (positional args)
- [x] `$#` (argument count)
- [x] `$*` / `$@` (all positional args -- and the real, easy-to-get-
      wrong difference between them under `"$*"` vs `"$@"` quoting)
- [x] `$?` (exit status of the last command) -- kept current per
      *segment*, not just once per line, so `false; echo $?` correctly
      sees `false`'s status rather than whatever the previous line
      left behind
- [x] `$$` (this shell's PID)
- [x] `$!` (PID of the last background job)
- [x] `$_` (last argument of the previous command)
- [x] `shift`
- [x] `set --` (rewriting positional parameters)
- [x] `PIPESTATUS`, `FUNCNAME`, `BASH_SOURCE`, `BASH_LINENO`,
      `BASH_VERSINFO`, `BASH_REMATCH`, `RANDOM`, `SECONDS`, `OSTYPE`,
      `HOSTNAME`, `UID`/`EUID`, `PPID`, and the rest bash scripts
      typically read (see the Variables table in the coverage doc for
      the full, measured list)


### Arithmetic
- [x] `$(( ))` arithmetic expansion
- [x] `(( ))` as a command/conditional (exit status from truthiness)
- [x] `let`
- [x] compound assignment inside arithmetic contexts (`+=`, `-=`,
      `*=`, `/=`, `%=`, `++`, `--`)


### Arrays
- [x] indexed arrays: `arr=(a b c)`, `${arr[0]}`, `${arr[@]}`,
      `${#arr[@]}`, slices, negative/arithmetic subscripts, per-element
      operators
- [x] associative arrays: `declare -A`, `${assoc[key]}` -- kept in
      insertion order; bash's own is its hash order, which no script
      may rely on either
- [x] namerefs: `declare -n`/`local -n`, `+n`, `unset -n` -- chained,
      element targets (`declare -n e='a[1]'`), `for` rebinding


### Parameter expansion (string manipulation)
- [x] `${var:-default}` / `${var:=default}` / `${var:?msg}` /
      `${var:+alt}`
- [x] `${#var}` (string length) -- characters, not bytes, in a
      multibyte locale
- [x] `${var:offset}` / `${var:offset:length}` (substring)
- [x] `${var#pattern}` / `${var##pattern}` (remove shortest/longest
      matching prefix)
- [x] `${var%pattern}` / `${var%%pattern}` (remove shortest/longest
      matching suffix)
- [x] `${var/pat/repl}` / `${var//pat/repl}` (replace first/all)
- [x] `${var^}` / `${var^^}` / `${var,}` / `${var,,}` (case conversion)
- [x] `${!var}` (indirect reference), `${!prefix*}` / `${!prefix@}`
- [x] `${var@Q}` and the other `@` transform operators


### Builtins
- [x] `cd`, `exit`/`quit`, `help`, `.`/`source`
- [x] `read`, including `-a` (into an array), `-n`, `-d`, `-t`, `-p`
- [x] `export`, `unset`, `readonly`
- [x] `declare`/`typeset`, `local` -- `-p` in bash's exact format,
      `-a`/`-A`/`-i`/`-l`/`-u`/`-r`/`-x`/`-n`/`-g`; `declare -f`
      (printing a function's body) is the one unimplemented piece
- [x] `alias`/`unalias`
- [x] `trap`
- [x] `eval`
- [x] `exec` (replacing the shell process, and fd manipulation)
- [x] `getopts`
- [x] `set` (shell options: `-e`, `-u`, `-x`, `-o pipefail`, ...)
- [x] `shopt` (bash-specific shell options)
- [x] `type`, `command`, `builtin`, `hash`
- [x] `times`, `ulimit`, `disown`
- [x] `mapfile`/`readarray`
- [x] `printf %(fmt)T` (strftime-backed time formatting)
- [x] `enable` (turn builtins on/off), `compgen` (word-list generation
      -- genuinely useful outside interactive completion, since a
      script can call it directly)
- [x] `complete` (bash and fish flags), `compopt`, `bind` -- real, they
      drive the line editor (see the interactive section below)
- [x] `history` (persistent, `HISTFILE`), `fc -l` / `fc -s` (`fc` with an
      editor is refused)


### Interactive quality-of-life (GOALS' own "once POSIX is solid" tier)

Done, in `ledit.c`, `history.c`, `complete.c`, `compgen.c`, `compfish.c`,
`bind.c`, `prompt.c`, `rcfile.c` (details and measurements:
[docs/interactive.md](docs/interactive.md)):

- [x] a line editor: raw terminal mode, cursor movement, kill buffer, undo,
      multi-line editing of an unfinished construct, bracketed paste,
      `bind` / `~/.inputrc` key bindings
- [x] tab completion with a selectable menu: commands (cached `$PATH`
      table), paths, variables, jobs, `~user`, `CDPATH`, per-command specs
      (bash `complete` and fish-style flags, lazy `.vsh` files), case-
      insensitive and substring fallback matching
- [x] fish-style autosuggestion from history and syntax highlighting
      (command valid/invalid, quotes, existing paths, comments)
- [x] reverse search (`Ctrl-R`)
- [x] persistent history, `!!` / `!$` / `!n` / `!prefix` / `^a^b` expansion
- [x] `.bashrc`-style startup (`~/.vaporshellrc`, `~/.vsh_profile`, `/etc/vaporshell/vsh.conf`)
- [x] prompt escapes (`\u \h \w \$ ...`), `PROMPT_COMMAND`, `PROMPT_DIRTRIM`


Not done (also tracked in docs/interactive.md):

- [ ] `set -o vi` / `emacs` modes, kill ring (Alt-y), `HISTFILESIZE`/`HISTIGNORE`
- [ ] `fc` with an editor, `!` word modifiers (`:s/a/b/`), quote-aware word designators
- [ ] a graphical picker for Ctrl-R (it is readline-style incremental search)
- [ ] abbreviations, `autocd`/`..`, right prompt, Alt-Enter newline
- [ ] `$LINES`, Ctrl-C reaching a foreground child from inside the editor's raw mode
- [ ] completion specs for more external tools (only `git` and `ssh` ship, as optional files), hostname completion built in


### File Test Operators
- [x] `-e`, `-d`, `-f`
- [x] `-r`, `-w`, `-x` (readable/writable/executable)
- [x] `-h`/`-L` (symlink) -- blocked on NuttX itself: no symlink
      support at the VFS layer at all (confirmed directly, see
      `docs/c-posix-compatibility.md`), not something fixable in
      vaporshell/toybox alone; works via `lstat` on the standalone build
- [x] `-s` (non-empty)
- [x] `-nt` / `-ot` / `-ef` (newer/older/same-file)


### Comparison / String Comparison / Logical Operators (`test`/`[`)
- [x] `-eq` `-ne` `-lt` `-le` `-gt` `-ge`
- [x] `=` `!=` `<` `>`
- [x] `&&` `||` `!` (shell-level and `test`'s own internal `-a`/`-o`
      combinators)
