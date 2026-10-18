# The interactive shell

vaporshell is "bash-compatible, with fish/zsh-like quality-of-life features",
and the interpreter (see `bash-coverage.md`, `posix-coverage.md`) is well
ahead of the part you actually type into. This document is the plan and the
status for that part: the prompt, line editing, history, completion, and
startup files. Keep it current: every change to the interactive layer updates
the status tables below in the same commit.

## Principles

1. **Small.** Every feature sits behind a Kconfig/`make` switch and its cost is
   measured (`size`, see "Budget"). The minimal tier must stay a few KB on top
   of the interpreter; nothing is on by default that a 264 KB RP2040 cannot
   afford.
2. **Fast.** "Bash, but fast" is about the *interactive* path more than the
   interpreter: bash's completion is slow because it is shell code that forks
   `sed`/`grep`/`git`; fish's is fast because it is data plus native code.
   vaporshell follows fish: no fork on the Tab path unless a spec asks for one,
   no startup work that is not needed to print the first prompt, everything
   else loaded lazily.
3. **One editor, two platforms.** The same editor runs on Linux and on
   vaporOS. All OS differences go through `platform.h` (`vs_plat_tty_*`), like
   the rest of the shell. NuttX's `apps/system/readline` is *not* used by the
   editor: it cannot redraw under our control, so no suggestions, highlighting
   or pager, and it needs patches that have already stopped applying.
4. **Bash stays the contract.** Scripts and rc files written for bash keep
   working; fish-style features add to that and never change what a script
   sees. Interactive-only behaviour is never active in a script or `-c`.
5. **Plain data for configuration.** Colors, keys and behaviour are shell
   variables and `bind`/`complete` commands in an rc file, not a new config
   language.

## Where it stands (2026-10-18)

| Area | State |
|---|---|
| Interpreter (bash + POSIX modes) | essentially complete, see the coverage docs |
| Prompt | escapes, promptvars, `PROMPT_COMMAND`, `PROMPT_DIRTRIM` (Tier 1) |
| Line editing | own editor (`ledit.c`) on Linux and vaporOS; verified on the simulator and on the RP2040 over USB |
| History | in-memory + `HISTFILE` (default `~/.vsh_history`); `history` builtin; no `fc`, no `!!` |
| Completion | `complete.c`: command names from a cached sorted `$PATH` table, paths, `$VARIABLES`, and per-command specs registered with `complete` (`-W -A -f -d -P -S -X` and `-F` functions); asks before listing more than 100; `compgen` works in scripts; `bind` is accepted and ignored |
| Startup files | none read |
| Job control | `jobs`/`fg`/`bg`/`wait`/`disown`; no Ctrl-Z (NuttX has no process groups) |

## Feature list and status

✓ done, ◐ partial, ✗ not started. Update as work lands.

### Tier 1: a prompt and editing as good as bash's

| Feature | Status | Notes |
|---|---|---|
| `PS1`/`PS2` backslash escapes (`\u \h \H \w \W \$ \t \T \@ \A \d \! \# \j \s \v \V \n \r \e \a \\ \[ \]`, `\nnn`, `\D{fmt}`), also `${PS1@P}` | ✓ (`prompt.c`; `\l` prints `tty`) | `\[ \]` give the editor the true display width |
| `$PS1` parameter/command expansion (`promptvars`) | ✓ (no `shopt promptvars` switch yet) | |
| `PROMPT_COMMAND`, `PROMPT_DIRTRIM` | ✓ (`PROMPT_COMMAND` as a string, not an array) | |
| Raw-terminal seam in `platform.h` | ✓ (`vs_plat_tty_raw/read`, `vs_plat_rows`) | enter/leave raw mode, read a byte with timeout, columns |
| Editor core: insert/delete, cursor motion, UTF-8 aware, wide characters | ✓ (`ledit.c`) | `mb.c` already decodes characters |
| Emacs keys (Ctrl-A/E/B/F/D/H/K/U/W/Y/T/L, Alt-B/F/D/U/L/C/Backspace) | ✓ | |
| Arrows, Home/End/Delete, Ctrl/Alt-arrows | ✓ | |
| Kill ring and undo | ◐ undo ✓ (24 steps); one kill buffer, no Alt-y ring | |
| Multi-line buffer (continuation lines, long lines wrapping) | ✓ wrapping and `Alt-Enter` newlines; Up/Down do not move between buffer lines yet | redraw handles wrap |
| History: in-memory, `HISTSIZE`/`HISTFILE`/`HISTCONTROL` | ✓ (`history.c`); `HISTFILESIZE` and `HISTIGNORE` ✗; file is private (0600), appended on exit and rewritten to `HISTSIZE` lines when it grew past it | |
| `history` and `fc` builtins | ◐ | `history [n] -c -d -a -w -r -s` ✓; `fc` ✗ |
| Up/Down and prefix-search (PageUp/Down, Alt-p/n) | ✓ | |
| Ctrl-R incremental search | ✓ (Ctrl-S inside a search only; a non-search key ends it and is not re-run) | |
| `!!`, `!$`, `!n`, `^a^b^` history expansion | ✗ | interactive only; `set +H` |
| Ctrl-C at the prompt; Ctrl-C reaches the foreground child | ◐ prompt ✓ (at a `>` continuation prompt it drops the whole command, like bash); child ✗ (editor is out of raw mode while it runs, so the tty sends SIGINT as usual on Linux) | needs care without process groups |
| `COLUMNS`/`LINES`, SIGWINCH | ◐ | width is re-read on every redraw, so a resize heals at the next key; `$COLUMNS` fallback; no `$LINES` |
| Bracketed paste | ✓ (on by default on Linux; `VS_BRACKETED_PASTE=1` on vaporOS, off until verified there) | |

### Tier 2: startup files and configuration

| Feature | Status | Notes |
|---|---|---|
| `/etc/vaporshell/vsh.conf`, then `$VS_RC` or `~/.vaporshellrc` | ✓ (`rcfile.c`) | where `$HOME` is unset (vaporOS today) `/data` stands in |
| Login shell (`-l`, `--login`, or argv[0] starting with `-`) and `~/.vsh_profile` | ✓ | |
| `--rcfile FILE` (`--init-file`), `--norc` | ✓ | a failing rc file does not end the session; POSIX mode reads none |
| `~/.bashrc` | ✓ by design: never read implicitly | opt in with `source ~/.bashrc` from the rc file |
| `bind` for real (`bind '"\C-x": cmd'`, `bind -x`, `-l -p -P`), `~/.inputrc` subset | ◐ | currently inert |
| `$VS_*` look-and-feel variables (colors, suggestions on/off) | ◐ | `VS_AUTOSUGGEST`, `VS_HIGHLIGHT`, `VS_BRACKETED_PASTE`, `VS_COMPLETION_PATH`, `VS_RC`; colors are fixed |
| `set -o emacs` / `set -o vi` | ✗ | vi last |

### Tier 3: fish/zsh quality of life

| Feature | Status | Notes |
|---|---|---|
| Autosuggestion from history (grey text; Right/End/Ctrl-F/Ctrl-E accepts) | ✓ | newest matching entry; `VS_AUTOSUGGEST=0` turns it off; accepting one word (Alt-F) ✗ |
| Syntax highlighting while typing (valid command / path / quote / error) | ◐ | command words only: green runs something (keyword, alias, function, builtin, cached `$PATH`, executable path), red does not; `VS_HIGHLIGHT=0` turns it off. Paths, quotes and errors ✗ |
| Completion menu: grid with selection, Shift-Tab, descriptions | ✗ | |
| Option-aware completion (`git <Tab>`, `ls -<Tab>`) | ◐ | any command with a `complete` spec; specs load lazily from `$VS_COMPLETION_PATH` (`<cmd>.vsh`, default `~/.config/vaporshell/completions:/etc/vaporshell/completions`) the first time the command is completed, and the shell's own commands have built-in defaults (`cd` → directories, `unset` → variables, `help` → builtins ...). No specs ship for external tools yet; fish-style `-s/-l/-d/-n` flags ✗ |
| Completion of variables, `~user`, jobs, `cd` CDPATH, hostnames | ◐ | `$VAR` and `${VAR}` ✓; the rest ✗ |
| Case-insensitive and substring/fuzzy fallback matching | ✗ | |
| Abbreviations (`abbr`) | ✗ | |
| `..` / `...` / `cd -` / `autocd` | ✗ | |
| Multi-line paste and Alt-Enter newline | ✗ | |
| Right prompt, transient prompt | ✗ | opt-in, via a shell function |
| `vi` mode | ✗ | |

### Out of scope

- Ctrl-Z and real job control: NuttX has no process groups
  (`libs/libc/unistd/lib_setpgid.c`).
- Clipboard: still an open question in `design.md`.
- zsh glob qualifiers, `setopt`, themes/plugin managers. The rc file and
  functions are the extension mechanism.

## Architecture

```
vaporshell_main.c   read loop: expand prompt -> editor -> parser (unchanged contract)
prompt.c            PS1/PS2/PS4 escapes, PROMPT_COMMAND; no terminal needed (testable alone)
ledit.c / ledit.h   the line editor: buffer, keymap, redraw, kill ring, undo, search, bracketed paste
ledit_hl.c          highlighting + suggestion rendering (separate unit so it can be left out)
history.c           history store, HISTFILE load/save, `history`, `fc`, `!` expansion
complete.c          candidate generation (shared by Tab, compgen and the menu)
comp_spec.c         declarative specs: `complete -c cmd -s/-l/-a/-n ...`, lazy autoload
rcfile.c            which startup files, in which order
platform.h          vs_plat_tty_raw(), vs_plat_tty_read(), ... (NuttX + posix implementations)
```

The editor's contract with the rest of the shell is the existing one:
`char *readline(prompt)` semantics (a malloc'd line with its newline, NULL at
EOF), reached through the same single call site, so the parser, the
continuation prompt and `vaporshell < file` are untouched. When stdin is not a
terminal the editor is bypassed entirely.

Output is composed into one buffer per keystroke and written with a single
`write()` (the USB CDC console wedges on byte-at-a-time output; see
`vaporOS/patches/nuttx/cdcacm-tx-ring-race.patch`). The editor keeps only the
current line plus a bounded history; there is no per-key allocation.

### Platform seam

`platform.h` gains:

- `vs_plat_tty_raw(bool on)`: switch the input terminal to raw (no echo, no
  line buffering, no signal characters except what we map ourselves).
  Posix: `termios`. NuttX: `termios` where `CONFIG_SERIAL_TERMIOS`, otherwise
  a no-op because NuttX serial devices already deliver bytes immediately.
- `vs_plat_tty_read(unsigned char *c, int timeout_ms)`: one byte, with a
  timeout (needed to tell a lone ESC from an escape sequence).
- `vs_plat_columns()` (exists), `vs_plat_rows()`.

## Completion: how it stays fast

What makes bash completion slow is that the common case is a shell function
(`_git`, `_ssh`, ...) that forks helper processes for every Tab. The design
here is the opposite:

1. **Native sources, zero forks.** Command names (builtins, functions, aliases,
   `$PATH`), files and directories, variables, `~user`, jobs, signals, `help`
   topics, shell options are all produced in C.
2. **A cached command table.** `$PATH` is scanned once into a sorted array;
   each Tab does one `stat()` per `$PATH` directory and rescans only the ones
   whose mtime changed. A lookup is a binary search.
3. **Declarative specs, not functions.** Per-command completion is *data*
   (`complete -c git -n '__vs_seen_subcommand' -a 'commit push ...' -d ...`),
   fish-style flags accepted by the same `complete` builtin that already takes
   bash's `-F`/`-W`/`-A`. A spec is a few bytes and is evaluated in C.
4. **Lazy loading.** Specs for `cmd` are read from `$VS_COMPLETION_PATH`
   (`~/.config/vaporshell/completions`, `/etc/vaporshell/completions`, ...)
   the first time `cmd` is completed, never at startup. Startup cost stays
   proportional to the rc file, not to the number of supported tools.
5. **Dynamic candidates by built-in generators** (git branches by reading
   `.git/refs` and `packed-refs`, not by running `git`). Where a spec does need
   a process, it runs under a time budget (default 200 ms); the menu appears
   with what is ready and says so.
6. **Bash compatibility is the slow lane, and it is bounded.** `complete -F fn`
   and `bash-completion` scripts work (`COMP_WORDS`, `COMP_CWORD`, `COMPREPLY`),
   but under the same time budget, and users who want speed use specs.
7. **Bounded work.** Candidate lists are capped, matching is incremental, and
   the menu renders only the visible window.

Licensing note: fish's completions are GPL-2 and cannot be copied into
vaporshell. vaporOS will ship its own specs; `tools/` may later get a *local*
converter that users run on their own fish install.

## Startup files

Interactive shell, in order, each only if it exists:

1. `/etc/vaporshell/vsh.conf` (system defaults; vaporOS image).
2. `$VS_RC`, else `~/.vaporshellrc`. On the vaporOS simulator the interim home
   is the hostfs-backed `/data`; this must move to `$HOME` once real storage
   and home directories exist (tracked in `design.md`, "Open questions").
3. Login shells read `~/.vsh_profile` first.

`~/.bashrc` is **not** read implicitly: bash rc files assume bash-only
completion machinery and slow prompt hooks. `source ~/.bashrc` in the rc file
opts in. `--norc` and `--rcfile FILE` behave as in bash. Non-interactive shells
read nothing (unchanged).

## Budget and measurement

Recorded per tier with `size build/vaporshell` (text+data) and, for the speed
claims, `tests/bench/`:

| Metric | Target |
|---|---|
| Interpreter only (before this work, Linux x86-64, -O2) | 239.5 KB text |
| + minimal editor, history, prompt escapes, rc files | ≤ +25 KB. Measured 2026-10-18: **+25.4 KB** (ledit 12.6, history 3.6, prompt 3.7, rcfile 0.9, complete 3.0 which was not built on Linux before) → 264.9 KB; the completion work above added 4 KB more (268.9 KB), then lazy specs, suggestions and highlighting: **272.7 KB** in all (+33 KB over the interpreter; the budget was +25 minimal, +20 for suggestions/highlighting/menu). Everything is switched by a variable at run time, not compiled out yet |
| + autosuggest, highlighting, menu | ≤ +20 KB, each separately switchable |
| Time to first prompt (no rc) | ≤ bash's, measured |
| Tab on `$PATH` command prefix, warm cache | < 1 ms. **Measured 2026-10-18: 0.07–0.11 ms** (`make -f posix.mk bench LINE=ec`), was 1.5–2.0 ms before the cache; 30 `$PATH` directories, 4,500 names |
| Tab in a directory of 1000 entries | < 10 ms |

Numbers go in this table as they are measured, with the date and commit.
Not yet measured: time to first prompt, and Tab latency on the RP2040's filesystem.

## Testing

- `prompt.c` is pure and covered by ordinary differential tests against bash
  (`PS1='\u@\h:\w\$ ' ; echo "${PS1@P}"`).
- The editor is tested through a pseudo-terminal: `tests/pty/run.py` (`make -f
  posix.mk check-pty`, also part of `check-all`) drives the real binary with
  keystrokes and asserts on the screen, rendered through a small VT model
  (`tests/pty/ptytest.py`). 37 tests today. The NuttX simulator is covered by
  `tests/nuttx-sim-smoke.py`, which runs through the same editor (it
  understands the editor's redraws); the pty tests do not run against the
  simulator or the RP2040 yet.
- Every editor feature has a test in the same commit that adds it.

## Inspirations (cloned next to this repo)

| Project | What to take |
|---|---|
| fish (`fish-shell/src/reader`, `pager.rs`, `highlight`, `complete.rs`) | suggestions, pager layout, lazy autoloading, completion descriptions |
| linenoise, bestline, isocline | the smallest workable redraw loop; isocline for UTF-8/wide-char width, bracketed paste, history search UI |
| zsh `Src/Zle` | keymaps and widgets, to model `bind` |
| bash readline (`bash/lib/readline`) | exact key semantics, `inputrc` syntax |
| ohmyzsh, starship | what users actually put in prompts (git segment, duration, exit status) |
