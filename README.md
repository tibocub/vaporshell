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

```
vaporshell/
  vaporshell_main.c    entry point + interactive loop
  tokenize.c            whitespace/quote tokenizing
  dispatch.c            the tbx multicall command table
  help.c                the help builtin
  script.c              script-file execution
  builtins.c            cd, ./source, help
  exec.c                PATH resolution + posix_spawnp
  expand.c              $VAR/${VAR} expansion, assignment
  subst.c                command substitution ($(...), `...`)
  line.c                 splits/runs a line (;, &&, ||)
  pipeline.c             cmd1 | cmd2 | cmd3 execution
  control.c              if/then/elif/else/fi
  loops.c                for/while/until
  case.c                 case/esac
  control_internal.h     shared control-flow scanning
  vaporshell.h          shared header
  Kconfig, Makefile     standard NuttX app-directory shape
  docs/design.md        the real design doc -- read this first for
                        anything non-trivial
```

Symlinked into [vaporOS-nuttx](https://github.com/tibocub/vaporOS-nuttx)
as `vaporshell/`, the same pattern `vaporOS-coreutils` uses for
`toybox/` -- see that repo's own `setup.sh` for how it gets cloned and
wired in automatically.


## STATUS

Interactive and script use both work: tokenizing, quoting (including
mid-token, e.g. `name="tibo smith"`), backslash escaping, `#`
comments, `$VAR`/`${VAR}`/`$?` expansion, command substitution
(`$(...)` and `` `...` ``), variable assignment, `;`/`&&`/`||`/`|`
(real pipelines), `.`/`source`, `test`/`[` (comparison, string
comparison, and file test operators), and full control flow --
`if`/`then`/`elif`/`else`/`fi`, `for`/`while`/`until`/`done`, and
`case`/`esac` -- all working single-line, multi-line, and nested. See
the TODO below for the real, current gap list -- kept accurate and
updated as things get implemented, not left to go stale.

Also worth knowing: `CONFIG_LINE_MAX` (vaporOS-nuttx's own build.sh)
needs to be raised from NuttX's own default of 80 for any of this to
be usable interactively past a short line -- `readline()` itself
allocates exactly `LINE_MAX` bytes and silently truncates anything
longer, confirmed directly the hard way (a `while` loop typed on one
line kept losing its own trailing text at exactly the 79th character
until this was raised).

## TODO

Goal for this list specifically: comprehensive enough that filling
every box would mean vaporshell is very close to fully bash-compatible
-- a reference/roadmap, not a near-term commitment. The project's own
stated priority (see GOALS above) stays POSIX-first, bash
quality-of-life second; this list exists so nothing bash actually does
is invisible while that priority order gets worked through. Updated
whenever something gets implemented -- if this drifts from reality,
treat that as a bug in the list, not in the code.

**Two corrections from the previous version of this list**, found
while implementing `test`/`[`:
- `[[ ]]` was checked off, but isn't actually true: `[[` is not
  registered in vaporshell's own dispatch table (`test` and `[` are;
  `[[` isn't), so `[[ ... ]]` fails with "command not found" today.
  Even once registered, toybox's own `test.c` (which is what runs
  under all three names) doesn't implement `[[`'s *real* bash
  semantics -- unquoted `<`/`>` without needing escaping, no word
  splitting or globbing on the arguments, `=~` regex matching with
  bash's own quoting rules -- it just treats `[[` as another name
  alias for plain `test`. Real `[[ ]]` needs its own parsing path in
  vaporshell, not just a dispatch table entry.
- `( )` was checked off, but that's conflating it with `$(...)`
  (command substitution, genuinely done). Subshells -- `( cmd1; cmd2 )`
  running in an isolated child environment, e.g. so a `cd` inside
  doesn't affect the calling shell -- were never implemented at all.

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
- [ ] redirection: `>`, `<`, `>>`, `2>`, `2>&1`, `&>`, `n>&m`
- [ ] here-documents (`<<`) and here-strings (`<<<`)
- [ ] process substitution (`<(...)`, `>(...)`)
- [ ] subshells: `( cmd1; cmd2 )` in an isolated child environment
- [ ] command grouping: `{ cmd1; cmd2; }` in the *current* environment
      (distinct from subshells -- no isolation, just sequencing)
- [ ] globbing / wildcard expansion (`*`, `?`, `[...]`)
- [ ] brace expansion (`{a,b,c}`, `{1..5}`)
- [ ] tilde expansion (`~`, `~user`)
- [ ] background execution (`&`) and `jobs`/`fg`/`bg`/`wait` -- real
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
- [ ] `break` / `continue` (including `break N` / `continue N`)
- [ ] real `[[ ... ]]` semantics (see correction above)

### Functions
- [ ] `name() { ...; }` / `function name { ...; }`
- [ ] `local` (function-scoped variables)
- [ ] `return`
- [ ] recursion

### Special variables / positional parameters
- [ ] `$0` (script/shell name), `$1`-`$9`, `${10}`+ (positional args)
- [ ] `$#` (argument count)
- [ ] `$*` / `$@` (all positional args -- and the real, easy-to-get-
      wrong difference between them under `"$*"` vs `"$@"` quoting)
- [x] `$?` (exit status of the last command) -- kept current per
      *segment*, not just once per line, so `false; echo $?` correctly
      sees `false`'s status rather than whatever the previous line
      left behind
- [ ] `$$` (this shell's PID)
- [ ] `$!` (PID of the last background job)
- [ ] `$_` (last argument of the previous command)
- [ ] `shift`
- [ ] `set --` (rewriting positional parameters)

### Arithmetic
- [ ] `$(( ))` arithmetic expansion
- [ ] `(( ))` as a command/conditional (exit status from truthiness)
- [ ] `let`
- [ ] compound assignment inside arithmetic contexts (`+=`, `-=`,
      `*=`, `/=`, `%=`, `++`, `--`)

### Arrays
- [ ] indexed arrays: `arr=(a b c)`, `${arr[0]}`, `${arr[@]}`,
      `${#arr[@]}`
- [ ] associative arrays: `declare -A`, `${assoc[key]}`

### Parameter expansion (string manipulation)
- [ ] `${var:-default}` / `${var:=default}` / `${var:?msg}` /
      `${var:+alt}`
- [ ] `${#var}` (string length)
- [ ] `${var:offset}` / `${var:offset:length}` (substring)
- [ ] `${var#pattern}` / `${var##pattern}` (remove shortest/longest
      matching prefix)
- [ ] `${var%pattern}` / `${var%%pattern}` (remove shortest/longest
      matching suffix)
- [ ] `${var/pat/repl}` / `${var//pat/repl}` (replace first/all)
- [ ] `${var^}` / `${var^^}` / `${var,}` / `${var,,}` (case conversion)
- [ ] `${!var}` (indirect reference)

### Builtins
- [x] `cd`, `exit`/`quit`, `help`, `.`/`source`
- [ ] `read`
- [ ] `export`, `unset`, `readonly`
- [ ] `declare`/`typeset`, `local`
- [ ] `alias`/`unalias`
- [ ] `trap`
- [ ] `eval`
- [ ] `exec` (replacing the shell process, and fd manipulation)
- [ ] `getopts`
- [ ] `set` (shell options: `-e`, `-u`, `-x`, `-o pipefail`, ...)
- [ ] `shopt` (bash-specific shell options)
- [ ] `type`, `command`, `builtin`, `hash`
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
- [ ] `-r`, `-w`, `-x` (readable/writable/executable)
- [ ] `-h`/`-L` (symlink) -- blocked on NuttX itself: no symlink
      support at the VFS layer at all (confirmed directly, see
      `docs/c-posix-compatibility.md`), not something fixable in
      vaporshell/toybox alone
- [ ] `-s` (non-empty)
- [ ] `-nt` / `-ot` / `-ef` (newer/older/same-file) -- these are
      already implemented in the ported `test.c` itself (upstream
      toybox has them); just not yet exercised/confirmed on-device

### Comparison / String Comparison / Logical Operators (`test`/`[`)
- [x] `-eq` `-ne` `-lt` `-le` `-gt` `-ge`
- [x] `=` `!=` `<` `>`
- [x] `&&` `||` `!` (shell-level; `test`'s own internal `-a`/`-o`
      combinators are also already in the ported source, not yet
      separately confirmed)
