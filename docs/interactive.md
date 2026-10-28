# The interactive shell

vaporshell is "bash-compatible, with fish/zsh-like quality-of-life features",
and the interpreter (see `bash-coverage.md`, `posix-coverage.md`) is well
ahead of the part you actually type into. This document is the plan and the
status for that part: the prompt, line editing, history, completion, and
startup files. Keep it current: every change to the interactive layer updates
the status tables below in the same commit.

## Principles

1. **Small.** Every feature's cost is measured (`size`, see "Budget") and
   anything useful to only some users is an optional `.vash` file, not code.
   Behaviours that cost run time switch off with a variable (`VASH_AUTOSUGGEST`,
   `VASH_HIGHLIGHT`, ...). There are no compile-time switches: the whole editor,
   completion and fish specs are 17 + 6 + 3 KB of flash, and nothing allocates
   until it is used, while the constraint on the RP2040 is its 264 KB of RAM,
   not its flash. Add switches only if flash ever gets tight.
2. **Fast.** "Bash, but fast" is about the *interactive* path more than the
   interpreter: bash's completion is slow because it is shell code that forks
   `sed`/`grep`/`git`; fish's is fast because it is data plus native code.
   vaporshell follows fish: no fork on the Tab path unless a spec asks for one,
   no startup work that is not needed to print the first prompt, everything
   else loaded lazily.
3. **One editor, two platforms.** The same editor runs on Linux and on
   vaporOS. All OS differences go through `platform.h` (`vash_plat_tty_*`), like
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
| History | in-memory + `HISTFILE` (default `~/.vash_history`); `history` builtin; `fc -l/-s`; `!` expansion (see below) |
| Completion | `complete.c`: command names from a cached sorted `$PATH` table, paths, `$VARIABLES`, and per-command specs registered with `complete` (`-W -A -f -d -P -S -X` and `-F` functions); asks before listing more than 100; `compgen` works in scripts; `bind` is real (`bind.c`) |
| Startup files | `/etc/vaporshell/vash.conf`, `~/.inputrc`, `~/.vashrc` (`$VASH_RC`), then the bash files unless `LOAD_BASH_FILES=0`; `--norc`, `--rcfile` |
| Job control | `jobs`/`fg`/`bg`/`wait`/`disown`; no Ctrl-Z (NuttX has no process groups) |

## Feature list and status

✓ done, ◐ partial, ✗ not started. Update as work lands.

### Tier 1: a prompt and editing as good as bash's

| Feature | Status | Notes |
|---|---|---|
| `PS1`/`PS2` backslash escapes (`\u \h \H \w \W \$ \t \T \@ \A \d \! \# \j \s \v \V \n \r \e \a \\ \[ \]`, `\nnn`, `\D{fmt}`), also `${PS1@P}` | ✓ (`prompt.c`; `\l` prints `tty`) | `\[ \]` give the editor the true display width |
| `$PS1` parameter/command expansion (`promptvars`) | ✓ (no `shopt promptvars` switch yet) | |
| `PROMPT_COMMAND`, `PROMPT_DIRTRIM` | ✓ (`PROMPT_COMMAND` as a string, not an array) | |
| Raw-terminal seam in `platform.h` | ✓ (`vash_plat_tty_raw/read`, `vash_plat_rows`) | enter/leave raw mode, read a byte with timeout, columns |
| Editor core: insert/delete, cursor motion, UTF-8 aware, wide characters | ✓ (`ledit.c`) | `mb.c` already decodes characters |
| Emacs keys (Ctrl-A/E/B/F/D/H/K/U/W/Y/T/L, Alt-B/F/D/U/L/C/Backspace) | ✓ | |
| Arrows, Home/End/Delete, Ctrl/Alt-arrows | ✓ | |
| Kill ring and undo | ◐ undo ✓ (8 steps); one kill buffer, no Alt-y ring | |
| Multi-line buffer (continuation lines, long lines wrapping) | ✓ wrapping and `Alt-Enter` newlines; Up/Down do not move between buffer lines yet | redraw handles wrap |
| History: in-memory, `HISTSIZE`/`HISTFILE`/`HISTCONTROL` | ✓ (`history.c`); `HISTFILESIZE` and `HISTIGNORE` ✗; file is private (0600), appended on exit and rewritten to `HISTSIZE` lines when it grew past it | |
| `history` and `fc` builtins | ✓ | `history [n] -c -d -a -w -r -s`; `fc -l [-nr] [first [last]]` and `fc -s [old=new] [cmd]` (an event is a number, a negative offset or a prefix); `fc` with an editor is refused |
| Up/Down and prefix-search (PageUp/Down, Alt-p/n) | ✓ | |
| Ctrl-R incremental search | ✓ (Ctrl-S inside a search only; a non-search key ends it and is not re-run) | |
| `!!`, `!$`, `!n`, `!-n`, `!prefix`, `!?text?`, `!^ !*`, `:n`, `^a^b^` history expansion | ✓ (`history.c`) | interactive only, on the line as typed (the expanded line is shown, like bash); not inside single quotes or after a backslash; words split on blanks (quotes not understood); `set +H` turns it off; no `:s/a/b/` modifiers |
| Ctrl-C at the prompt; Ctrl-C reaches the foreground child | ◐ prompt ✓ (at a `>` continuation prompt it drops the whole command, like bash); child ✗ (editor is out of raw mode while it runs, so the tty sends SIGINT as usual on Linux) | needs care without process groups |
| `COLUMNS`/`LINES`, SIGWINCH | ◐ | width is re-read on every redraw, so a resize heals at the next key; `$COLUMNS` fallback; no `$LINES` |
| Bracketed paste | ✓ (on by default on Linux; `VASH_BRACKETED_PASTE=1` on vaporOS, off until verified there) | |

### Tier 2: startup files and configuration

| Feature | Status | Notes |
|---|---|---|
| `/etc/vaporshell/vash.conf`, then `$VASH_RC` or `~/.vashrc` | ✓ (`rcfile.c`) | where `$HOME` is unset (vaporOS today) `/data` stands in |
| Login shell (`-l`, `--login`, or argv[0] starting with `-`): `/etc/profile`, first of `~/.bash_profile` `~/.bash_login` `~/.profile` | ✓ | unless `LOAD_BASH_FILES=0` |
| `--rcfile FILE` (`--init-file`), `--norc` | ✓ | a failing rc file does not end the session; POSIX mode reads none |
| `~/.bashrc` | ✓ read after `~/.vashrc` | `LOAD_BASH_FILES=0` in `~/.vashrc` turns the bash files off |
| `bind` for real (`bind '"\C-x": kill-line'`, `bind -x '"\C-g": code'` with `READLINE_LINE`/`READLINE_POINT`, `-r -l -p`) | ✓ (`bind.c`) | keys are single keys or escape sequences, no chords, no macros; `~/.inputrc` (or `$INPUTRC`) is read before the rc file, and `bind -f FILE` reads one: only `"keys": command` lines count, `set`/`$if` and function names we lack are skipped silently. 36 editor commands under readline's names |
| `$VASH_*` look-and-feel variables (colors, suggestions on/off) | ◐ | `VASH_AUTOSUGGEST`, `VASH_HIGHLIGHT`, `VASH_BRACKETED_PASTE`, `VASH_COMPLETION_PATH`, `VASH_COMPLETION_TIMEOUT`, `VASH_RC`; colors are fixed |
| `set -o emacs` / `set -o vi` | ✗ | vi last |

### Tier 3: fish/zsh quality of life

| Feature | Status | Notes |
|---|---|---|
| Autosuggestion from history (grey text; Right/End/Ctrl-F/Ctrl-E accepts) | ✓ | newest matching entry; `VASH_AUTOSUGGEST=0` turns it off; Alt-F / Ctrl-Right at the end takes one word of it |
| Syntax highlighting while typing (valid command / path / quote / error) | ✓ | command words green (runs something) or red (does not); text in quotes yellow (from the opening quote to the closing one, or to the end of the line if it is not closed: no separate error colour); a `#` comment grey; arguments that are existing paths underlined (at most 16 `stat` calls per redraw); `VASH_HIGHLIGHT=0` turns it off. Redirection targets are checked like any argument; words with an expansion in them are not marked |
| Completion menu: grid with selection, Shift-Tab, descriptions | ✓ | opens under the line when Tab adds nothing; Tab/Shift-Tab/arrows move (column-major like bash), Enter takes the choice without running the line, Esc drops it, any other key closes it; scrolls ("rows a-b of n") when it does not fit; descriptions switch it to one column. Checked on the RP2040 |
| Option-aware completion (`git <Tab>`, `ls -<Tab>`) | ◐ | any command with a `complete` spec (bash flags, or fish flags `-c -s -l -o -d -a -n -f -x -r -e`); specs load lazily from `$VASH_COMPLETION_PATH` (`<cmd>.vash`, default `~/.config/vaporshell/completions:/etc/vaporshell/completions`) the first time the command is completed, and the shell's own commands have built-in defaults (`cd` → directories, `unset` → variables, `help` → builtins ...). The commands vaporOS runs through tbx (`ls`, `grep`, `sed`, `find` ... 52 of them) complete their options (`ls -<Tab>`): the option names are compiled in from `comp_defaults.h` (about 3 KB, NuttX build only), and `vaporOS-coreutils/completions/*.vash` has the same with a description per option, for `$VASH_COMPLETION_PATH`. Both are generated from toybox's own help text by `vaporOS-coreutils/scripts/gen-completions.py`. Anything useful only to some users is an optional `.vash` file, not code in the binary: `completions/git.vash` (subcommands, branches and tags) and `completions/ssh.vash` (hosts from `~/.ssh/config` and `known_hosts`) are in this repository. `kill -<Tab>` offers signal names (`-A signal`, names from the trap table). Fish-style specs (`compfish.c`): `-a` takes words (`word<TAB>text` gives a word its own description), not command substitutions; conditions are shell code run with `COMP_WORDS` set (slow lane: a function or condition is cut off after `$VASH_COMPLETION_TIMEOUT` ms, default 300, checked between commands, so one long external program still blocks); no `-k`, `--wraps` |
| Completion of variables, `~user`, jobs, `cd` CDPATH, hostnames | ✓ except hostnames | `$VAR`, `${VAR}`, `~`, `~/path`, `~user` (host builds: NuttX has no user database to read), `%N` jobs with their command shown (`fg`, `bg`, `kill`, `wait`, `disown` complete them by default), and `cd`/`pushd` also offer the directories under `$CDPATH`. They are also `compgen -A job`, `-A user` and `-A cdpath`. Hostnames are left to an optional `.vash` spec (there is no ssh on vaporOS to ask) |
| Case-insensitive and substring/fuzzy fallback matching | ✓ | when nothing starts with the word: names that start with it in another case, then names that contain it; they replace what was typed (the menu puts it back on Esc). Commands, files, directories and variables; not the candidates of a `complete` spec, which a spec filters itself. 0.2 ms worst case for a command |
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
platform.h          vash_plat_tty_raw(), vash_plat_tty_read(), ... (NuttX + posix implementations)
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

(NuttX: the console starts in canonical mode with echo, so its driver echoes keys, turns CR into LF and swallows backspace and delete. `vash_plat_tty_raw` clears ECHO, ICANON, ICRNL and ISIG through `tcsetattr`, which works without `CONFIG_SERIAL_TERMIOS`; that option only gates baud and VMIN/VTIME.)

`platform.h` gains:

- `vash_plat_tty_raw(bool on)`: switch the input terminal to raw (no echo, no
  line buffering, no signal characters except what we map ourselves).
  Posix: `termios`. NuttX: `termios` where `CONFIG_SERIAL_TERMIOS`, otherwise
  a no-op because NuttX serial devices already deliver bytes immediately.
- `vash_plat_tty_read(unsigned char *c, int timeout_ms)`: one byte, with a
  timeout (needed to tell a lone ESC from an escape sequence).
- `vash_plat_columns()` (exists), `vash_plat_rows()`.

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
4. **Lazy loading.** Specs for `cmd` are read from `$VASH_COMPLETION_PATH`
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

Interactive shell, in order, each only if it exists (none with `--norc`):

1. `/etc/vaporshell/vash.conf` (system defaults; vaporOS image), then `~/.inputrc`.
2. `--rcfile FILE` or `$VASH_RC`: that file, and nothing more.
3. Otherwise `~/.vashrc`: optional, for settings that must not touch the user's
   bash setup. It is read first so it can set `LOAD_BASH_FILES=0`.
4. Unless `LOAD_BASH_FILES=0` (default: on), the bash files: a login shell
   (`-l`, `--login`, argv[0] starting with `-`) reads `/etc/profile`, then the
   first of `~/.bash_profile`, `~/.bash_login`, `~/.profile`; any other
   interactive shell reads `~/.bashrc`.

On the vaporOS simulator the interim home is the hostfs-backed `/data`; this must
move to `$HOME` once real storage and home directories exist (tracked in
`design.md`, "Open questions"). Non-interactive shells read nothing.

## bash_completion

The system `bash_completion` (and its `completions/` files) loads and works when
sourced from `~/.vashrc`: `. /usr/share/bash-completion/bash_completion`. What it
needed, all now in: `;;&`/`;&` on their own line, multi-line extglob in `[[ ]]`,
`${!name-word}` and `${!1}`, `printf -v name[i]`, `local x` twice, `unset` of a
caller's local (the "upvars" idiom), `compgen -V` and `-W` with expansions,
`complete -D`/`-a`/`--` (a `-D` function answering 124 reloads the spec),
`compopt -o nospace/filenames`, `type -P`, `caller`, `$(<file)`, `COMP_TYPE`/`COMP_KEY`/
`COMP_WORDBREAKS`, and `complete -p` printing in bash's order. Also set at startup, as bash
does, because rc files test them: `$BASH`, `SHLVL`, a non-empty `PS1`/`PS2` in an interactive
shell, and `i` in `$-` for `-i` (even with `-c`). Not done: `compopt -o nosort/noquote`,
`complete -E`/`-I` specs being consulted, `set -x` tracing of `[[ ]]`, `$_`, and
`/etc/profile.d/which2.sh` (it keys on the executable being called bash or sh). It is slower than the native
completion (it is bash code), so it is opt-in: the native completions stay the default.

## Budget and measurement

Recorded per tier with `size build/vaporshell` (text+data) and, for the speed
claims, `tests/bench/`:

| Metric | Target |
|---|---|
| **On the device (RP2040, Thumb-2, -Os), 2026-10-18** | **all of vaporshell 121.9 KB; the interactive layer 24.6 KB** (ledit 9.2, compgen 5.6 (mostly older), complete 3.6, history 1.8, compfish 1.6, prompt 1.4, bind 1.0, rcfile 0.4). After `!` expansion, `fc`, `.inputrc`, signals and comments (2026-10-18, same method, from `libapps.a`): **interactive layer 27.8 KB (+3.2 KB)**; RAM unchanged (54.3 KB used at the prompt). Free RAM after boot with the shell running: 189 KB of 243 KB. The rows below are x86-64 numbers and run about 1.7x larger |
| Interpreter only (before this work, Linux x86-64) | 239.5 KB text at -O2, **168.9 KB at -Os** (the device build compiles for size, so -Os is the number that matters: `make -f posix.mk small`) |
| + minimal editor, history, prompt escapes, rc files | ≤ +25 KB. Measured 2026-10-18: **+25.4 KB** (ledit 12.6, history 3.6, prompt 3.7, rcfile 0.9, complete 3.0 which was not built on Linux before) → 264.9 KB; the completion work above added 4 KB more (268.9 KB), then lazy specs, suggestions and highlighting: **278.6 KB** in all (+39 KB over the interpreter; the budget was +25 minimal, +20 for suggestions/highlighting/menu). The interactive layer alone is **38.9 KB at -Os** (54.5 KB at -O2): whole binary 205 KB at -Os, 291 KB at -O2. `-Os` makes the interpreter about 13% slower on a pure loop benchmark, so the host default stays -O2. Compile-time switches were considered and dropped (see Principles); later additions: tbx option table, spec time limit and `bind` brought it to 285.5 KB (+46 KB: past the 45 KB combined budget), loose matching to 287.3 KB (+48 KB), path and quote highlighting to 289.1 KB (+50 KB) |
| + autosuggest, highlighting, menu | ≤ +20 KB, each separately switchable |
| Time to first prompt (no rc) | ≤ bash's, measured |
| Tab on `$PATH` command prefix, warm cache | < 1 ms. **Measured 2026-10-18: 0.07–0.11 ms** (`make -f posix.mk bench LINE=ec`), was 1.5–2.0 ms before the cache; 30 `$PATH` directories, 4,500 names |
| Tab in a directory of 1000 entries | < 10 ms |

Numbers go in this table as they are measured, with the date and commit.
Not yet measured: time to first prompt, and Tab latency on the RP2040's filesystem.

## Measured on the RP2040 (2026-10-18)

`vaporOS/tools/rp2040-bench.py` (`--setup 40` first makes a 40-file directory on littlefs). Times
include the USB round trip, 1.7 ms for one key. The RP2040 runs at 125 MHz; the filesystems are
binfs, procfs and littlefs on SPI flash.

| Action | Time |
|---|---|
| one key on an empty line / after 60 characters | 1.7 / 2.2 ms |
| one key with 5 `/flash` paths on the line | 1.9 ms (was 11.5 ms before paths were remembered) |
| Tab: command, tbx option, variable | 2.2, 2.7, 1.8 ms |
| Tab: nothing matches (three matching passes) / loose match | 3.3 / 4.3 ms |
| Tab: `cd /fl` to `/flash/` (was 30 ms: one `stat` per candidate, 10-40 ms each on littlefs) | 3.6 ms |
| Tab: menu of `/bin` or `/proc` (14 entries), choose next entry | 4.7 ms, 3.1 ms |
| Tab: 40-file littlefs directory (was 1,735 ms) | 212 ms (reading the directory: about 5 ms per entry) |
| Up arrow, with 100 history entries; Ctrl-R | 2.1 ms, 1.1 ms |
| first Tab after boot (command table built) | 2.4 ms |

Memory (243 KB heap, 16 KB shell stack): 54.1 KB in use with the shell idle, 189 KB free. 100 history
entries add 4.1 KB. A session of every Tab and menu above raised the most ever in use by 8.7 KB
(54.5 to 63.2 KB) and left 0.1 KB behind. The shell's stack high-water mark after all of it is 5.7 KB of 16.3 KB.
The things that cost time are the filesystem calls: `stat` on littlefs, and reading a big directory.

## Testing

- `prompt.c` is pure and covered by ordinary differential tests against bash
  (`PS1='\u@\h:\w\$ ' ; echo "${PS1@P}"`).
- The editor is tested through a pseudo-terminal: `tests/pty/run.py` (`make -f
  posix.mk check-pty`, also part of `check-all`) drives the real binary with
  keystrokes and asserts on the screen, rendered through a small VT model
  (`tests/pty/ptytest.py`). 49 tests today. The NuttX simulator is covered by
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
