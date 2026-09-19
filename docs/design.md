# vaporOS shell (`vaporshell`)

## Design principle

Same split as `vapor-api.md` and `vaporterm.md` (both in vaporOS-nuttx's
own `docs/` -- this repo is vaporshell's own split-off, see its
README): leverage what NuttX's own POSIX layer already solves, build
only what's genuinely shell-specific and unavailable from any OS
regardless of how POSIX-compliant it is. The first shells were a thin,
scriptable interface over the kernel -- not much more than "read a
line, resolve a path, run a program." That's the feeling to recreate:
vaporshell shouldn't
reimplement anything the kernel already does, it should just expose
it comfortably.

Portability is a first-class constraint from the start, not a later
pass: sim first for iteration speed, but every design decision below
is checked against whether it still holds on ESP32/RP2040, and NuttX
architecture-independent code (`sched/`, `libs/libc/`) is preferred
over sim-only shortcuts (`arch/sim/`) wherever both would work. Where
that isn't possible -- see Clipboard below -- it's called out
explicitly rather than quietly assumed to work everywhere.

## What NuttX already provides -- confirmed, not assumed

Checked directly against source, the same way `vapor-api.md` (in
vaporOS-nuttx's own docs/) checked what Lua's stdlib already covers:

- **PATH resolution + program execution**: `posix_spawn`/`posix_spawnp`
  are real (`sched/task/task_posixspawn.c`, `task_spawn.c`) --
  architecture-independent code, not under `arch/sim/`, so this isn't
  a sim-only convenience. `posix_spawnp`'s PATH search is built in via
  `CONFIG_LIBC_ENVPATH` (it's literally `#define posix_spawnp
  posix_spawn` with that flag set). This is the primitive to build on,
  not `fork()`+`exec()` -- NuttX's own docs list `fork()` itself as
  "not appropriate for a deeply-embedded RTOS," and confirmed directly
  that it only has a real implementation for the `sim` architecture
  (`arch/sim/src/sim/sim_fork.c`). `posix_spawn` is the one that
  actually works the same way on sim, ESP32, and RP2040.

- **Pipes and redirection**: `pipe()`/`dup2()` are real and already
  proven under load -- this is exactly what `vterm_fb`'s `openpty()`
  is built on.

- **Environment variables**: `getenv`/`setenv`/`environ` are real,
  per-task. `$PATH`, `$SHELL`, `$USER`, `$PWD` are plain environment
  variables, no special-casing needed.

- **`$PWD`**: `chdir`/`getcwd` are real, confirmed already (see
  vaporOS-nuttx's own `vapor-api.md`'s "WHY NUTTX" equivalent claims,
  and our own direct use of these this whole project).

- **Line editing, history, and raw-mode input**: `apps/system/readline`
  is already vendored, already fought through in detail getting
  `vterm_fb` working -- reuse this instead of writing a second line
  editor. See "Line editing" below for a real nuance this surfaced.

## Confirmed real gap: process groups

Checked directly in `libs/libc/unistd/lib_setpgid.c` -- its own
comment says it plainly: *"NuttX does not implement process groups,
so a process group always contains a single member and its ID equals
the process ID."* `setpgid`/`tcsetpgrp` exist but are stubs.

This is the mechanism real job control (`Ctrl+Z` suspending a whole
pipeline as a unit, `fg`/`bg` reassigning terminal control to a
process group) is built on in a real Unix shell, and it isn't
available here -- not a matter of effort, the underlying primitive
doesn't exist. **Job control is out of scope** for now. `Ctrl+C`
still works against a single foreground child directly (see below);
what doesn't work is the group semantics around it.

## Line editing: a nuance worth designing around, not just reusing blindly

`apps/system/readline` has real command history already built in
(`CONFIG_READLINE_CMD_HISTORY` -- up/down arrow recall, and even a
reverse incremental search under `CONFIG_READLINE_EDIT_EMACS_REVERSE_SEARCH`,
bash-style `Ctrl+R`). This is exactly the comfort NSH doesn't expose
today and vaporshell wants.

The catch: that interactive recall logic lives inside the
`CONFIG_READLINE_EDIT` branch -- the same branch `vterm_fb` had to
turn *off* entirely (`READLINE_EDIT=n`) to fix plain characters not
echoing when typed at the end of a line (see the vterm_fb debugging
history). Confirmed via source: `READLINE_EDIT`'s redraw-based
operations (history recall, Home/End, word movement) all trigger an
explicit `redraw_tail()`-style repaint, which does work correctly on
our terminal -- the bug was narrowly in the *plain append at
end-of-line* case (`cursor == nch`, so `redraw_tail`'s `cursor < nch`
guard never fires). vterm_fb's fix threw away the whole branch
(simplest fix, no NSH-specific behavior depending on history to
preserve) rather than patching that one condition.

vaporshell should not repeat that trade-off blindly. Worth actually
trying: `READLINE_EDIT=y` with a narrow patch to make the end-of-line
append case echo too (rather than disabling the branch wholesale),
which would get history recall, Ctrl+R, and word-movement for free
instead of rebuilding them. Needs a real test before deciding, not an
assumption either way -- flagged here specifically so this doesn't
get silently redecided the same way vterm_fb's tradeoff was, without
re-examining whether it still applies.

One shared-state detail from `readline`'s own Kconfig help text worth
noting: command history lives in one in-memory array shared by every
`readline()` caller in a FLAT build. If NSH and vaporshell ever ran
side by side, they'd share a history array. Probably irrelevant if
vaporshell fully replaces NSH's role rather than coexisting with it,
but worth knowing before assuming isolated history "just works."

## Architecture decision: parser and execution engine

**Decided:** a bash/fish-inspired shell aiming for real POSIX/bash
syntax and behavior where practical, not a custom Lua-syntax shell.
The Lua-as-shell-language idea (Xonsh/Nushell/PowerShell-style --
Lua's own syntax at the prompt, `vapor.*` for process control) was
considered and set aside: a non-POSIX syntax would cost about as much
custom parsing/lexing work as a real grammar, without the payoff of
actually being POSIX/bash-compatible. Execution stays in C via
`posix_spawnp`/pipes/the multicall table -- no second language
runtime in the execution path.

A real third-party option exists for the parser, once one is needed
beyond the whitespace/quote tokenizing Milestone 1 uses:
[`mrsh`](https://mrsh.sh) (emersion, MIT, C99) is a strict POSIX shell
whose parser and AST are explicitly exposed as `libmrsh`, a standalone
public interface -- its own stated purpose is being "a library to
build more elaborate shells," not just a monolithic shell binary.
`mrsh -n script.sh` dumps the parsed AST without executing it,
confirming the parser and interpreter are genuinely separable -- we'd
want the former (real POSIX grammar: pipes, redirection, quoting,
control flow) and write our own executor tied to `posix_spawn`/the
multicall table/`vapor`, not adopt mrsh's own execution engine. Real
caveats: long quiet stretches in its history (most activity
2018-2022), links `librt`, Meson-first build needing adaptation into
this repo's Makefile/Kconfig shape -- same class of porting spike as
toybox, not assumed to just work until actually tried.

**`system()`/`os.execute()`, confirmed rather than assumed:** checked
directly in `apps/system/system/system.c`. The default path is
genuinely hardcoded -- `task_spawn("system", nsh_system, ...)` calls
NSH's own command function directly, no name resolution involved at
all. But NuttX ships a real, already-built escape hatch:
`CONFIG_SYSTEM_SYSTEM_SHPATH`, which switches `system()` to a real
`posix_spawn()` against whatever program name that's set to, calling
it as `{shpath, "-c", cmd, NULL}` -- the exact convention
`vaporshell -c` now implements. The name resolves through NuttX's
binfmt loader chain (`binfmt/builtin.c` matches against the compiled-
in builtin-apps table -- confirmed by reading it directly), the same
mechanism NSH itself uses to resolve command names, so no new
plumbing is needed there. Redirecting `system()`/`os.execute()` here
is just `kconfig-tweak --set-str CONFIG_SYSTEM_SYSTEM_SHPATH
vaporshell` once vaporshell's bareword-command support is solid
enough that things calling `system("some nsh command")` expecting
NSH's own syntax don't quietly break.

## Feature list

### Core execution

- [x] PATH resolution (`posix_spawnp` + `CONFIG_LIBC_ENVPATH`)
- [x] Program execution (`posix_spawn`, not `fork`+`exec`)
- [x] Multicall dispatch table: falls back to it only on `ENOENT`
      from `posix_spawnp` (a real installed program with the same
      name still wins, same precedence a real Unix shell gives
      `$PATH` over any builtin utility replacement), spawning `tbx`
      with `argv[0]="tbx"` and the original typed command prepended
      as `argv[1]` -- **not** the `argv[0]`-rewriting convention
      originally planned below (superseded, see "Toybox port" below
      for why: toybox's own `main()`/`toybox_main()` turned out to be
      unusable directly once NuttX's own `<PROGNAME>_main` renaming
      collided with it, so the real entry point became `tbx`'s own
      `vapor_entry.c`, which expects this different argv shape).
      Table is hand-maintained in `vaporshell_main.c`
      (`g_tbx_commands[]`), not generated from any build config --
      reasonable at 19 commands, worth revisiting if that list grows
      much longer. Covers both toybox- and NSH-ported commands alike
      (vaporOS-coreutils' own `toys/` vs `nsh-ports/` split); this
      table doesn't distinguish the two, since that split only matters
      for diffing each against its own upstream, not for dispatch.
- [x] Builtins -- `cd` implemented (mutates the shell's own state, so
      it can never be a spawned program regardless of how good
      NuttX's spawn story is). `export`, others: not yet.
- [x] `$?` (last exit status) -- shell-internal bookkeeping, no OS
      equivalent to lean on.
- [x] `-c <command>` non-interactive mode -- also what
      `CONFIG_SYSTEM_SYSTEM_SHPATH` needs to redirect `system()`/
      `os.execute()` here instead of NSH (see "Architecture decision"
      above); not built for that alone, any real shell needs this.

**Confirmed while implementing, worth being explicit about:** checked
the actual generated `apps/builtin/builtin_list.h` from a real build
-- NSH's own commands (`ls`, `cat`, `pwd`, `cd`, ...) are internal to
`nshlib`'s own command table, not separate entries in the builtin-apps
table `posix_spawn` resolves against. Toybox (`tbx`) now fills this
gap for the applets it implements (`true`, `false`, `echo`, `pwd`,
`cat`, `mkdir`, `rmdir`, `touch`, `printf`, `rm`) via the multicall
table above; anything else still spawns only if it's a separately-
registered `apps/external` program (`vhello`, `portable_wc`,
`portable_cat`, `vlua`, `vi`, `vaporshell` itself).

### Environment / variables

- [ ] `$PATH`, `$SHELL`, `$USER`, `$PWD` as real environment variables
- [ ] Dynamic prompt, built from shell-internal state (cwd, exit
      status, ...) -- pure string interpolation, no OS dependency

### Comfort NSH lacks

- [ ] rc file for configuration (aliases, prompt format, env
      defaults). Note this is genuinely two different stories right
      now: on sim, hostfs-backed `/data` already works today and could
      hold this immediately; on real hardware, persistence depends on
      the littlefs/FAT32 backends the README's own ROADMAP still marks
      `[WIP]`/unstarted. Fine to build against `/data` on sim first,
      just don't assume the same path is available yet on ESP32/RP2040.
- [ ] History navigation (`Up`/`Down`, `Ctrl+P`/`Ctrl+N`) -- see "Line
      editing" above, this may be substantially free depending on how
      that's resolved
- [ ] Tab completion, for both PATH commands and filesystem paths --
      new work either way; needs the multicall table (for commands)
      and `opendir`/`readdir` (for paths, both real per vaporOS-nuttx's
      own `vapor-api.md`'s own confirmed-gaps section on the Lua side)
- [ ] `Ctrl+L` (clear screen) -- straightforward, shell-internal
- [ ] `Ctrl+C` (interrupt the running foreground child) -- works
      against a single child directly via its pid; without process
      groups, a multi-stage pipeline can't be interrupted as one unit
      the normal way, only by signaling each spawned pid individually.
      Worth designing for explicitly rather than discovering the gap
      later.
- [ ] `Ctrl+A`/`Ctrl+E` (line start/end) -- straightforward cursor
      movement, shell-internal
- [ ] `Ctrl+Shift+C`/`Ctrl+Shift+V` (copy/paste) -- **open question,
      not a settled feature.** On sim, this only means something if
      it's targeting the X11 window `vterm_fb` renders into directly,
      and keyboard input isn't even captured from that window today
      (input is forwarded from the host terminal you launched
      `./nuttx` from -- see vaporOS-nuttx's own `vaporterm.md`'s staged
      input plan). Pasting into that *host terminal* already works
      today, for free, with no vaporshell-specific work, since it
      arrives as ordinary forwarded
      bytes. On ESP32/RP2040 with a physical keyboard, "clipboard" may
      not correspond to anything real at all. Needs a decision on what
      this feature actually means per-target before it's designed, not
      after.

## Toybox port

Split into its own repo, [vaporOS-coreutils](https://github.com/tibocub/vaporOS-coreutils),
once it grew from a compatibility port into a real fork (own
defaults, own commands beyond what either toybox or NSH provide).
Cloned as a sibling by `setup.sh`, referenced here via `toybox/`
being a symlink to it. See that repo's own `docs/porting-notes.md`
for the real NuttX-portability bugs found and fixed during the
original spike/port.

## Open questions

- Does `READLINE_EDIT=y` + a narrow end-of-line-echo patch actually
  work cleanly for vaporshell, or does something else in that branch
  also assume NSH-specific behavior we'd need to rip out? Needs a real
  test, not a read of the source alone.
- Where vaporshell's rc file lives on sim right now (`/data/...`,
  hostfs-backed) vs. where it should live once real hardware storage
  exists -- worth deciding the sim-only interim path explicitly so it
  doesn't quietly become the permanent one.

**Answered:** multicall dispatch table format/ownership -- hand-
maintained (`g_tbx_commands[]` in `vaporshell_main.c`), not generated
from any build config. Reasonable at the current scope (19 commands);
revisit if that list grows much longer, since hand-maintaining it
means it can drift out of sync with vaporOS-coreutils' own
`Makefile`/`CSRCS` list if one gets updated without the other --
already happened once (this table briefly still said "10 applets"
after coreutils grew past that; fixed while writing this doc's own
move into this repo).

## Milestones (proposed, mirroring vaporOS-nuttx's own `vaporterm.md`'s staging)

1. Core loop: read a line (reusing `readline_common.c`, decision on
   `READLINE_EDIT` pending the test above), tokenize (whitespace +
   basic quoting only, no expansion yet), resolve PATH or the
   multicall table, `posix_spawn`, wait, print `$?`. No pipes,
   redirection, or rc file yet -- the smallest thing that can run a
   real command end to end.
2. Redirection and pipes (`dup2`, `pipe`) -- proven primitives, mostly
   sequencing work at this point.
3. Comfort layer: dynamic prompt, rc file (sim/`/data` first), tab
   completion.
4. `Ctrl+C`/`Ctrl+L`/`Ctrl+A`/`Ctrl+E`, history (contingent on the
   `READLINE_EDIT` decision above).
5. Clipboard -- only after the open question above is actually
   answered, not before.


## Architecture (engine rewrite)

The first version re-scanned command text: every feature carried its own
quote-tracking loop, quoting was lost at tokenization (`echo '$x'"$x"`
expanded wrongly), expansion was strictly one token to one argument (so no
field splitting, globbing or `"$@"` was possible), and variables lived in
`environ`. That could not grow into POSIX, let alone bash, so the front end
was replaced. Layers, each calling only downward:

    input lines --> lexer --> parser --> AST --> executor --> platform
                      |         |                  |  ^
                      +- wordscan.c (one place that knows quoting)
                                       expand.c ---+  (words -> fields)
                                       vars.c, builtins.c, redir.c

- **Words stay raw** in the AST (quotes and `$` constructs intact).
  `wordscan.c` finds where `'..'`, `".."`, `${..}`, `$(..)`, `$((..))` and
  backticks end; both the lexer and the expander use it. `$(` is scanned by
  running the real parser (a `)` inside a `case` pattern is legal).
- **Expansion builds fields whose characters each carry a "quoted" flag.**
  Unquoted expansion results are IFS-split as they are added, unquoted glob
  characters are active in pathname expansion, and quote removal is just
  dropping the flags.
- **Input is line-callback based**, so the same parser serves the
  interactive prompt (PS1/PS2), script files (any line length), `-c`
  strings and `eval`. It asks for another line only when a construct cannot
  end yet, and heredoc bodies are read when the newline after `<<EOF` is
  consumed.
- **Control flow is state, not jumps:** `g_sh.unwind` (break / continue /
  return / exit) is checked after each command. `set -e` is a counter of
  contexts where failure is expected.
- **Redirections** are applied in the shell process and undone afterwards,
  so builtins, functions, compound commands and external programs share one
  mechanism.
- **Platform layer** (`platform.h`): the only code that differs between
  NuttX and a host OS. Host: `fork` for subshells, `&`, pipelines and
  command substitution, own PATH search, `posix_spawn`. NuttX: no `fork`
  (only the sim has one), so those constructs are limited -- an in-process
  subshell (snapshot/restore of variables, cwd and fds) is the intended fix
  and is not built yet.
- **Out of memory is fatal** (`vs_xmalloc`), which keeps every caller free
  of NULL checks.

### Modes

Superseded by `docs/modes.md`, which has the measured bash / `bash --posix`
/ dash differences, the feature-bit model (`mode.h`), the extension points
for bash's larger features, the module layout, and the plan for the
interactive layer and plugins.
