# vaporshell

> The shell of [vaporOS](https://github.com/tibocub/vaporOS-nuttx)

A thin, scriptable interface over NuttX's own POSIX layer -- read a
line, resolve a path, run a program. Doesn't reimplement anything the
kernel already does; the design principle throughout is "expose what's
already there comfortably" rather than build a shell's worth of new
machinery. See `docs/design.md` for the full reasoning, feature list,
and open questions -- this file is the short version.


## GOALS

- A real POSIX shell first: tokenizing, quoting, `$VAR` expansion,
  redirection, control flow (`if`/`for`/`while`), functions -- the
  actual `sh` language, not a look-alike. This is the priority; a
  correct, if incomplete, POSIX shell beats a shell that half-does
  POSIX and half-does something else.
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
  platform.h          what differs between NuttX and a host OS
  platform_nuttx.c    NuttX: no fork, posix_spawnp, tbx fallback (dispatch.c)
  posix/              standalone build only: platform.c (fork, PATH, spawn),
                      readline.c, and stand-in <nuttx/...> headers
  posix.mk            standalone build (see BUILD)
  Kconfig, Makefile   standard NuttX app-directory shape
  docs/design.md      the design doc -- read this first for anything non-trivial
  tests/              own/ (vs bash and dash), modes/ (per-mode suites),
                      check-all.sh, smoosh corpus + smoosh-check.sh,
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
sh tests/nuttx-symcheck.sh ../nuttx/staging      # global-name collisions
python3 tests/nuttx-sim-smoke.py ../nuttx/nuttx  # run it in the simulator
```

Everything in `libapps.a` shares one flat symbol namespace, so give any new
global a `vs_`/`g_vs_` style name; `symcheck` finds the ones that clash
(the first version collided with NuttX's own `g_builtins`).

Standalone (Linux, macOS, BSD), from a plain checkout:

```
make -f posix.mk            # build/vaporshell   (or just `make`)
make check                  # every suite, each vs its reference shell
                            # (BASH=/path/to/bash selects the bash reference)
make coverage               # regenerate docs/*-coverage.md from the probes
vaporshell --posix          # POSIX mode (also -o posix, or invoked as sh)
make check-smoosh           # the smoosh corpus in --posix mode, regressions named
make check-asan             # same tests under ASan + UBSan
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
presets of feature bits over one engine (`mode.h`, `docs/modes.md`). Only a
handful of bits exist so far -- each one is a measured difference with a
test -- and bash's larger features (`[[`, arrays, brace expansion, ...) are
not built yet.

Working: quoting, all POSIX expansions (parameter operators, `$(...)`,
backticks, `$(( ))`, tilde, field splitting, pathname expansion), all
POSIX redirections and here-documents, pipelines, `&&`/`||`/`!`, `;`/`&`,
`if`/`for`/`while`/`until`/`case`, `{ }`, `( )`, functions, positional
parameters, `break`/`continue N`/`return`, `set -e -u -x -f -C -a -n -v`, `trap`
(EXIT and signals), aliases, `$LINENO`, and the builtins in `help`.

How close each mode is to its reference is measured, not claimed: see
[docs/bash-coverage.md](docs/bash-coverage.md) (bash 5.3.0) and
[docs/posix-coverage.md](docs/posix-coverage.md) (dash 0.5.12), regenerated
with `python3 tests/coverage/gen-docs.py build/vaporshell --bash /path/to/bash`.

Smoosh corpus (`make check-smoosh`, run in `--posix` mode): 146 of 184 by a
rough pass rule (see `tests/smoosh-check.sh`); dash scores 147 and
`bash --posix` 143 by the same rule. (Two tests call bash's `source`, which
POSIX mode deliberately does not have.) The rest are listed in `tests/smoosh-known-failures.txt`.

Platform notes: on the standalone build `( )`, `&`, pipelines with
builtins/functions, and `$(...)` fork. NuttX has no `fork()`, so there
those cases are limited: `$(...)` runs a child `vaporshell -c` (and
variables are exported so it can see them), a pipeline stage must be an
external program (or a builtin tbx also provides: `true`, `false`, `pwd`,
`test`, `[`), and `( )`, `&`, `exec` and other builtins in a pipeline
report "not supported on this platform yet". An in-process subshell is the
intended fix.

NuttX: built and run in the simulator against `releases/13.0` with the
vaporOS `sim:nsh` configuration (`tests/nuttx-sim-smoke.py` drives it).
Real-hardware targets have not been tried.

Also worth knowing: `CONFIG_LINE_MAX` (vaporOS-nuttx's own build.sh)
still needs to be raised from NuttX's default of 80 for interactive input,
because `readline()` there truncates at exactly `LINE_MAX`. Script files
and `-c` strings have no such limit.

## TODO

Goal for this list specifically: comprehensive enough that filling
every box would mean vaporshell is very close to fully bash-compatible
-- a reference/roadmap, not a near-term commitment. The project's own
stated priority (see GOALS above) stays POSIX-first, bash
quality-of-life second; this list exists so nothing bash actually does
is invisible while that priority order gets worked through. Updated
whenever something gets implemented -- if this drifts from reality,
treat that as a bug in the list, not in the code.

**Note on `[[ ]]`:** `[[` is a keyword with its own grammar (no word
splitting or globbing inside, unquoted `<`/`>`, `=~`), not just another
name for `test`. It needs its own parsing path in the parser; `test` and
`[` are done and are in the builtin table.

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
- [x] redirection: `>`, `<`, `>>`, `2>`, `2>&1`, `&>`, `n>&m` -- `&>` (bash) not yet
- [x] here-documents (`<<`) and here-strings (`<<<`) -- `<<` and `<<-` done; here-strings (bash) not yet
- [ ] process substitution (`<(...)`, `>(...)`)
- [x] subshells: `( cmd1; cmd2 )` in an isolated child environment
- [x] command grouping: `{ cmd1; cmd2; }` in the *current* environment
      (distinct from subshells -- no isolation, just sequencing)
- [x] globbing / wildcard expansion (`*`, `?`, `[...]`)
- [ ] brace expansion (`{a,b,c}`, `{1..5}`)
- [x] tilde expansion (`~`, `~user`)
- [x] background execution (`&`) and `jobs`/`fg`/`bg`/`wait` -- real -- `&` and `wait` done (standalone build only); no job control (`jobs`/`fg`/`bg`) yet
      job control depends on process groups, which NuttX itself only
      stubs (see `docs/c-posix-compatibility.md` in vaporOS-nuttx);
      worth revisiting what's actually achievable here specifically
      before committing to it

### Control flow
- [x] `if`/`then`/`elif`/`else`/`fi` -- works both on one line
      (`;`-separated) and across multiple (the common script style,
      with an interactive continuation prompt too), including nested
      if/fi and pipes/`test`/`[` inside conditions. A genuinely
      tricky feature to get right -- four separate, real bugs found
      and fixed along the way: a naive word-boundary check treating
      punctuation like `-` as a keyword boundary (so `echo
      multiline-then-ok` was misread as containing the real keyword
      "then"); a missing leading-newline skip that broke detecting a
      *nested* if specifically (its own extracted body text starts
      with the newline that followed the outer "then"); the same
      leading-whitespace issue in the marker-scanning pass itself; and
      the construct not being separated from text that follows its
      own closing "fi" on the same line/string, which silently
      discarded any trailing statements until fixed
- [x] `for NAME in LIST; do ...; done` -- no bare "for x; do" (no "in
      list", positional parameters) yet, positional parameters aren't
      implemented; real, reported error rather than a silent guess
- [ ] `for ((init; cond; step)); do ...; done` (C-style, bash-specific)
- [x] `while ...; do ...; done`
- [x] `until ...; do ...; done`
- [x] `case ... in ... esac` -- pattern matching supports literal
      text, `*`, `?`, and `|` alternation; bracket expressions
      (`[abc]`/`[a-z]`) are a real, known gap, not implemented
- [ ] `select ...; do ...; done` (bash-specific menu construct)
- [x] `break` / `continue` (including `break N` / `continue N`)
- [ ] real `[[ ... ]]` semantics (see correction above)

### Functions
- [x] `name() { ...; }` / `function name { ...; }` -- `name() { ...; }` done; the bash `function name` form not yet
- [ ] `local` (function-scoped variables)
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
- [ ] `$_` (last argument of the previous command)
- [x] `shift`
- [x] `set --` (rewriting positional parameters)

### Arithmetic
- [x] `$(( ))` arithmetic expansion
- [ ] `(( ))` as a command/conditional (exit status from truthiness)
- [ ] `let`
- [x] compound assignment inside arithmetic contexts (`+=`, `-=`, -- `+=` `-=` etc. done; `++`/`--` not yet
      `*=`, `/=`, `%=`, `++`, `--`)

### Arrays
- [ ] indexed arrays: `arr=(a b c)`, `${arr[0]}`, `${arr[@]}`,
      `${#arr[@]}`
- [ ] associative arrays: `declare -A`, `${assoc[key]}`

### Parameter expansion (string manipulation)
- [x] `${var:-default}` / `${var:=default}` / `${var:?msg}` /
      `${var:+alt}`
- [x] `${#var}` (string length)
- [ ] `${var:offset}` / `${var:offset:length}` (substring)
- [x] `${var#pattern}` / `${var##pattern}` (remove shortest/longest
      matching prefix)
- [x] `${var%pattern}` / `${var%%pattern}` (remove shortest/longest
      matching suffix)
- [ ] `${var/pat/repl}` / `${var//pat/repl}` (replace first/all)
- [ ] `${var^}` / `${var^^}` / `${var,}` / `${var,,}` (case conversion)
- [ ] `${!var}` (indirect reference)

### Builtins
- [x] `cd`, `exit`/`quit`, `help`, `.`/`source`
- [x] `read`
- [x] `export`, `unset`, `readonly`
- [ ] `declare`/`typeset`, `local`
- [ ] `alias`/`unalias`
- [x] `trap`
- [x] `eval`
- [x] `exec` (replacing the shell process, and fd manipulation)
- [ ] `getopts`
- [x] `set` (shell options: `-e`, `-u`, `-x`, `-o pipefail`, ...) -- `-e -u -x -f -C` and `-o` names done; `pipefail` not yet
- [ ] `shopt` (bash-specific shell options)
- [x] `type`, `command`, `builtin`, `hash` -- `type` and `command` done; `builtin` and `hash` not yet
- [ ] `times`, `ulimit`, `disown`

### Interactive quality-of-life (GOALS' own "once POSIX is solid" tier)
- [ ] tab completion
- [ ] `.bashrc`-style startup file
- [ ] persistent history across sessions (readline's own
      `CONFIG_READLINE_CMD_HISTORY` already gives in-session history;
      persisting it to a file is separate, still open)
- [ ] `!!`, `!$`, `!N`-style history expansion

### File Test Operators
- [x] `-e`, `-d`, `-f`
- [x] `-r`, `-w`, `-x` (readable/writable/executable)
- [x] `-h`/`-L` (symlink) -- blocked on NuttX itself: no symlink -- works via `lstat` on the standalone build; still blocked on NuttX itself
      support at the VFS layer at all (confirmed directly, see
      `docs/c-posix-compatibility.md`), not something fixable in
      vaporshell/toybox alone
- [x] `-s` (non-empty)
- [x] `-nt` / `-ot` / `-ef` (newer/older/same-file) -- these are
      already implemented in the ported `test.c` itself (upstream
      toybox has them); just not yet exercised/confirmed on-device

### Comparison / String Comparison / Logical Operators (`test`/`[`)
- [x] `-eq` `-ne` `-lt` `-le` `-gt` `-ge`
- [x] `=` `!=` `<` `>`
- [x] `&&` `||` `!` (shell-level; `test`'s own internal `-a`/`-o`
      combinators are also already in the ported source, not yet
      separately confirmed)
