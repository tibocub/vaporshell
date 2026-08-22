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
  vaporshell_main.c   the shell itself
  Kconfig, Makefile    standard NuttX app-directory shape
  docs/design.md        the real design doc -- read this first for
                        anything non-trivial
```

Symlinked into [vaporOS-nuttx](https://github.com/tibocub/vaporOS-nuttx)
as `vaporshell/`, the same pattern `vaporOS-coreutils` uses for
`toybox/` -- see that repo's own `setup.sh` for how it gets cloned and
wired in automatically.


## STATUS

Interactive use works: tokenizing, quoting, builtins (`cd`, `exit`,
`help`), `$PATH` resolution, environment variables. Scripting (reading
a file of commands rather than a line at a time) doesn't exist yet --
see `docs/design.md`'s own milestones for what's next.

## TODO

**Basic shell features**:
- [x] execute a script file
- [x] source a script into the current session (. and source)
- [ ] variables (num=3; echo $num)

**Bash synthax**:
- [ ] ";" end line with semicolon
- [ ] bash datatypes ( age=5; name="tibo")
- [ ] "$" process variables and execute commands inside a string (echo $PWD; echo "I'm in $(pwd)"; if -z [[ "$string" ]]; etc.)
- [ ] ( )
- [ ] [ ]
- [ ] [[ ]]
- [ ] if/then/else

**Comparison Operators**:
- [ ] -eq: Equal to
- [ ] -ne: Not equal to
- [ ] -lt: Less than
- [ ] -le: Less than or equal to
- [ ] -gt: Greater than
- [ ] -ge: Greater than or equal to

**String Comparison Operators**:
- [ ] =: Equal to
- [ ] !=: Not equal to
- [ ] <: Less than, in ASCII alphabetical order
- [ ] >: Greater than, in ASCII alphabetical order

**Arithmetic Operators**:
- [ ] +: Addition
- [ ] -: Subtraction
- [ ] *: Multiplication
- [ ] /: Division
- [ ] %: Modulus

**Logical Operators**:
- [ ] &&: Logical AND
- [ ] ||: Logical OR
- [ ] !: Logical NOT

**File Test Operators**:
- [ ] -e: Checks if a file exists
- [ ] -d: Checks if a directory exists
- [ ] -f: Checks if a file is a regular file
- [ ] -s: Checks if a file is not empty
