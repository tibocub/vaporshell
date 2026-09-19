# Language modes, extensibility and the interactive layer

Status legend used below: **[built]** exists and is tested, **[measured]** a
fact checked against real shells, **[plan]** a design not yet implemented.
Nothing marked [plan] should be read as a promise about the API; it is the
current best structure, to be revised when the first real user of it exists.

## 1. The goal

One binary, one engine. `vaporshell` behaves like bash by default;
`vaporshell --posix` (or `-o posix`, or being invoked as `sh`, or
`POSIXLY_CORRECT` in the environment) behaves like POSIX `sh`. Everything
the two share is written once. Later, other bash-like dialects -- or an
original language -- should be additions, not forks.

## 2. Measured differences [measured]

Probed with bash 5.2.21, `bash --posix`, and dash 0.5.12 (Ubuntu 24.04; the
target is bash 5.3 -- re-measure the bash columns when a 5.3 is at hand), and
vaporshell before/after this change (`tests/modes/` holds each as a test).
"POSIX here" means what the standard requires; where `bash --posix` and dash
disagree the strict reading (dash) is what our POSIX profile follows.

| Behaviour                                      | bash    | bash --posix | dash         | vaporshell now    |
|------------------------------------------------|---------|--------------|--------------|-------------------|
| error in a special builtin exits               | no      | yes          | yes          | mode-dependent    |
| `x=1 :` keeps `x`                              | no      | yes          | yes          | mode-dependent    |
| function overrides a special builtin           | yes     | no           | no           | mode-dependent    |
| syntax error in `eval` exits                   | no      | yes          | yes          | mode-dependent    |
| `.` falls back to the current dir              | yes     | no           | no           | mode-dependent    |
| function name `foo-bar`                        | allowed | rejected     | rejected     | mode-dependent    |
| `$(...)` inherits `set -e`                     | no      | yes          | yes          | mode-dependent    |
| `[ a == a ]`                                   | true    | true         | error        | mode-dependent    |
| `&>file` is a redirect                         | yes     | **yes**      | no (`&`,`>`) | mode-dependent    |
| `break` in a function leaves the caller's loop | no      | no           | no           | fixed (was wrong) |
| `set -e; x=$(false)` exits                     | yes     | yes          | yes          | fixed (was wrong) |

Two things this table taught us, both now recorded as tests:

- Until this change vaporshell was a *hybrid*: POSIX-like on the first
  five rows, bash-like on `==`. Our tests passed against bash, dash and
  vaporshell only because they exercised the common ground; nothing forced a
  choice. Now every row above is a named feature (`mode.h`) and a test.
- `bash --posix` is not strict POSIX (`&>`, `==`, brace expansion stay on).
  So "POSIX mode" needs its own reference: `tests/modes/posix/` is checked
  against `bash --posix`, `tests/modes/posix-strict/` against dash.

Still bash-only and not built (each will become a feature bit, [plan]):
`[[ ]]`, `(( ))`, arrays and associative arrays, `${x:o:l}` `${x/p/r}`
`${x^^}` `${!x}`, brace expansion, process substitution, `<<<`, `$'...'`,
`local`/`declare`/`typeset`, `function name {`, `time`, `select`,
`coproc`, `;&` `;;&`, extglob, `**` and `++`/`--` in arithmetic, `$RANDOM`
`$BASH_*`, aliases in scripts (POSIX requires them, so this one is a
POSIX-side gap too). Missing POSIX pieces: `$LINENO`, `getopts`, `hash`,
`alias`, `echo`/`printf` builtins, `ulimit`, `times`.

## 3. The model [built]

`mode.h`: `enum vs_feature_e` is a list of independent behaviours; a
*profile* is a preset of bits (`mode.c`, the whole definition of "bash" and
"posix"). The core asks `vs_feat(VF_X)` at the one place the behaviour
differs. Rules for adding a bit: measure first and cite it in the table
above; name it for the behaviour, not the shell; ask at one site; never test
the profile itself. `set -o posix` / `+o posix` just call `vs_mode_set()`.

Because a mode is data, individual bits can later be switched on their own
(bash's `shopt` is exactly this), and a new dialect is a new preset plus
whatever extension code it registers.

Where features are consulted today: the lexer (operator table has a
per-operator feature), the parser (`is_func_name`), the executor
(`vs_special_error`, command lookup order, assignment persistence, `$(...)`
errexit), the builtin table (each entry has a `modes` mask; `builtin_find`
and `help` filter by it) and `test`.

## 4. Extension points for bash's larger features [plan]

Feature bits are enough for behaviour switches. Bash's bigger constructs
need code that POSIX-only builds should not carry, so the core exposes a
small number of *registries* instead of `#ifdef`s -- five, chosen because
they are the places these features actually attach:

1. **Builtins** [built as a table]: bash-only ones (`declare`, `mapfile`,
   `shopt`, ...) are entries with `VS_M_BASH`.
2. **Compound commands / keywords** [plan]: `[[`, `((`, `select`, `coproc`,
   `function`, `time` register `{keyword-or-token, feature, parse, exec}`.
   The parser asks the registry where it currently switches on `if`/`while`.
3. **Expansion operators and stages** [plan]: extra `${...}` operators
   (`/`, `:o:l`, `^^`, `!`), and whole stages (brace expansion before
   parameter expansion, process substitution) as registered functions.
4. **Special variables** [plan]: `RANDOM`, `SECONDS`, `LINENO`, `BASH_*` as
   getter/setter pairs in a table, mode-tagged.
5. **Redirection operators** [built for `&>`]: the lexer's operator table is
   already feature-gated; `<<<` and `<( )` follow the same route.

Arrays and variable attributes (`declare -i -r -x -A`) are the one item that
is not just registration: the variable table (`vars.c`) must grow attributes
and typed values first. That is deliberately the first bash-layer task.

## 5. Other dialects and an original language [plan]

A dialect is: a feature preset + the registry entries it enables. That
covers bash-like shells (ksh, zsh-in-sh-emulation) up to the point where
their *grammar* differs; then it needs its own front end that produces the
same AST, which the same executor runs. An original language is likewise a
new front end (or a new AST plus executor). What the executor should not be
asked to do is understand syntax: keep the parser -> AST boundary clean and
it stays possible. We do not build any of this speculatively.

## 6. Modular builds [plan]

Target layout (files move only when the second dialect or the UI layer
actually needs the boundary):

    core/      lexer parser ast expand exec redir vars glob arith mode traps
               (POSIX engine; depends on nothing below)
    bash/      bash-only registry entries and builtins   (depends on core)
    ui/        line editor, completion, highlighting, history (depends on the
               core's lexer/parser API only; the executor never calls it)
    plugins/   loader and plugin API                     (depends on core)
    platform/  posix/ and nuttx/                          (as today)

Each of `bash/`, `ui/`, `plugins/` is a Kconfig option (NuttX) and a `make`
switch (standalone), so "next-gen completion costs a few MB" is a choice at
build time, not a tax. Rule: the core must build and pass the POSIX suites
with all three off. `tests/nuttx-symcheck.sh` and a size report per
configuration belong in CI so this stays true.

## 7. The interactive layer -- "fish-like" [plan]

Requirements this puts on the *core* (cheap now, painful to retrofit):

- **Tolerant lexing for highlighting**: a function that classifies spans of
  a partial command line (command word, builtin/function/external/unknown,
  string, variable, redirection, error) without executing anything. The
  lexer already reports "input incomplete" (`err_eof`); add a span-emitting
  mode of the same code, not a second lexer.
- **Completion providers** with a *time budget*: each provider returns
  candidates in slices and can be told to stop; results are cached (the
  `$PATH` command index, directory listings, per-command completion data).
  The line editor never waits for a provider.
- **No threads required**: NuttX may have no fork but has tasks/pthreads;
  we should not depend on either. The editor is a `poll()` loop over stdin
  plus incremental providers, so a slow `stat` or a big `$PATH` costs
  frames, not freezes.
- **History** as its own store with an index (fuzzy / prefix search, visual
  reverse search), independent of the readline in use.

Responsiveness rule to hold ourselves to: no builtin or editor action may
block on anything but the terminal or the command it is asked to run. That
is testable (a latency budget per keypress in a pty test) and should be.

## 8. Plugins [plan]

Two forms, one API:

- **Static plugins**: compiled into the binary, registered at startup.
  This is the vaporOS / single-binary case and needs no loader.
- **Dynamic plugins**: loaded at runtime (`dlopen` on a host; on NuttX the
  ELF loader with the app symbol table that vaporOS already enables -- not
  yet verified for this purpose).

Both see the shell only through a **versioned table of function pointers**
(`struct vs_api`), never through internal symbols, so the core can change
without breaking plugins. A plugin exports `vs_plugin_init(const struct
vs_api *api)` and may register: builtins, completion providers, prompt
segments, special variables, and event hooks (`precmd`, `preexec`, `chpwd`,
`exit`). Everything a plugin registers is namespaced and removable, which
is also what makes "compile without it" honest. Native code means a plugin
can crash the shell; that is accepted for trusted plugins, and the API
should not pretend otherwise.

## 9. One binary with vaporOS-coreutils [plan, risky]

Registering the coreutils as in-process builtins would remove a process
spawn per command, but only if each command is *re-entrant and never calls
`exit()` directly*. Toybox keeps global state and exits from many places, so
the realistic first step is the multicall arrangement NuttX already uses
(one binary, separate task per command, tbx), extended so the shell can run
a chosen set in-process behind a wrapper that traps `exit`. Measure a
specific command (`cat`, `wc`) end to end before committing to the approach.

## 10. Order of work

1. **[built]** Mode model, `--posix`, measured differences as tests, `&>` as
   the first gated syntax, two real bugs fixed.
2. Finish POSIX mode: `LINENO`, `getopts`, `hash`, `alias`, `echo`/`printf`
   (both behaviours), `ulimit`, `times`, remaining smoosh failures.
3. Bash layer: variable attributes and arrays, then the registries in
   section 4, then `[[`, `((`, expansions, `local`/`declare`.
4. Interactive core (section 7), then the editor.
5. Plugin API, first with static plugins.
6. Coreutils fusion experiments.

Open decisions: whether the POSIX profile should ever offer a "bash --posix
compatible" variant (today: strict); how plugins get loaded on NuttX; how
much of section 7 belongs in the core repository versus separate ones.
