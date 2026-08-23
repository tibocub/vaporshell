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
- [x] variables (num=3; echo $num)

**Bash synthax**:
- [x] ";" end line with semicolon
- [x] bash datatypes ( age=5; name="tibo")
- [x] "$" process variables and execute commands inside a string (echo $PWD; echo "I'm in $(pwd)"; if -z [[ "$string" ]]; etc.)
- [x] ( )
- [x] [ ]
- [x] [[ ]]
- [ ] if/then/else

**Arguments**:
- [ ] $#	Number of arguments
- [ ] $*	All positional arguments (as a single word)
- [ ] $@	All positional arguments (as separate strings)
- [ ] $1	First argument
- [ ] $!	Insert last argument of previous command in current command (i.e: mkdir test; cd !$)
- [ ] $_	Last argument of the previous commandé

**Comparison Operators**:
- [x] -eq   Equal to
- [x] -ne   Not equal to
- [x] -lt   Less than
- [x] -le   Less than or equal to
- [x] -gt   Greater than
- [x] -ge   Greater than or equal to

**String Comparison Operators**:
- [x] =     Equal to
- [x] !=    Not equal to
- [x] <     Less than, in ASCII alphabetical order
- [x] >     Greater than, in ASCII alphabetical order

**Arithmetic Operators**:
- [ ] +     Addition
- [ ] -     Subtraction
- [ ] *     Multiplication
- [ ] /     Division
- [ ] %     Modulus

**Logical Operators**:
- [x] &&    Logical AND
- [x] ||    Logical OR
- [x] !     Logical NOT

**File Test Operators**:
- [x] -e    Checks a file exists
- [ ] -r    Checks a file is readable
- [ ] -w    Checks a file is writable
- [ ] -x    Checks a file is executable
- [ ] -h    Checks a file is a symlink
- [x] -d    Checks a directory exists
- [x] -f    Checks a file is a regular file
- [ ] -s    Checks a file is not empty (byte size > 0)
- [ ] -nt   Checks a file in newer than another
- [ ] -ot   Checks a file in older than another
- [ ] -ef   Checks two files are the same
- [ ] 
- [ ] 
