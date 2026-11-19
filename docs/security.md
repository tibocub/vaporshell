# Security practices

How vaporshell follows the [OpenSSF](https://openssf.org/) guidance, and how to
reproduce each piece. Reporting a vulnerability: see `SECURITY.md`.

## Checked in CI (`.github/workflows/`)

| What | Where | Notes |
|---|---|---|
| Build + every differential suite + pty tests, gcc and clang | `ci.yml` `test` | the reference `dash` is 0.5.12 on the Ubuntu runner, which the POSIX suite is modelled on |
| ASan + UBSan run of all suites | `ci.yml` `sanitizers` | `make check-asan` |
| Fuzzing, 90 s per push/PR, from the seed corpus | `ci.yml` `fuzz` | crashes are uploaded as an artifact |
| Static analysis | `codeql.yml` | CodeQL for C, weekly and on every PR |
| Supply-chain score | `scorecard.yml` | OpenSSF Scorecard, results in the Security tab |
| Updates of pinned actions | `dependabot.yml` | every action is pinned to a commit SHA |

Workflows run with `contents: read` only and do not persist the checkout
token. Vaporshell has no third-party code dependency (libc and pthreads only),
so there is nothing else to pin.

## Hardened build

`make` (standalone POSIX build) adds, when the compiler accepts them:
`-D_FORTIFY_SOURCE=2`, `-fstack-protector-strong`, `-fstack-clash-protection`,
`-fcf-protection`, `-fno-strict-overflow`, `-fPIE`, and links `-pie`,
`-z relro -z now`, `-z noexecstack`. `make HARDEN=0` turns them off.
`make strict` builds with `-Werror` and distro fortify off.

Check a binary: `readelf -lW build/vaporshell | grep -E 'RELRO|STACK'`.

## Fuzzing

`fuzz/fuzz.c` is a libFuzzer harness (clang, ASan + UBSan). One binary; the
first input byte picks the target. Targets 0-10 run no commands; target 11 runs scripts in a sandbox (below).

| Byte % 12 | Target |
|---|---|
| 0 | lexer + parser over a script |
| 1 | arithmetic evaluator (inputs with `$` or backtick skipped) |
| 2 | glob matching (pattern and subject split at the first newline) |
| 3 | brace expansion |
| 4 | history `!` expansion |
| 5 | word expansion (skipped if the text could run a command) |
| 6 | the `printf` builtin (`-v` only) |
| 7 | the `test` / `[` builtin |
| 8 | the line editor's syntax highlighter |
| 9 | Tab completion of the input as a command line |
| 10 | the `bind` builtin (key sequences, readline commands) |
| 11 | the input run as a script in a sandbox: `PATH` is read-only and points nowhere, so no program can start; inputs with paths, redirections, loops, functions, `exec`, `exit`, `cd`, `trap`, ... are skipped |

```sh
make -f posix.mk fuzz
mkdir -p /tmp/work && cp -r fuzz/corpus /tmp/work/corpus
build-fuzz/fuzz /tmp/work/corpus -max_total_time=300 -timeout=10 -jobs=4 -workers=4
```

`fuzz/corpus/` is the seed set (the test scripts, prefixed with a target byte).
A crashing input is a bug: attach it to a private report if it looks
exploitable, otherwise fix it and add the input to `tests/` as a regression.

`fuzz/regress/` holds the inputs that crashed an earlier version; CI replays them
(`build-fuzz/fuzz fuzz/regress/*`).

Bugs found so far (all fixed):

- `{1..666666666666666666666}`: signed overflow while reading the number
  (brace.c). Sequence bounds are now limited to 17 digits.
- `printf '%((%Y'`: one byte read past the end of the format when a `%(...)T` was
  not closed (builtins_io.c).
- `printf '%99999999999s'`: a width past INT_MAX made `snprintf` fail and the result
  was used as a length: heap over-read (builtins_io.c). It now reports an error.
- `PS1='${BASH_COMMAND} $ '`: `$BASH_COMMAND` read the command of the previous line
  after its memory was freed: use-after-free (exec.c). The pointer is restored to the
  enclosing command when a line or a function body ends.
- Completion menu: file names (and descriptions) were written to the terminal as they
  are, so a file called `x<ESC>]0;...` could drive the terminal when you pressed Tab.
  Control bytes (and the C1 range) are now shown as `?`; the grey autosuggestion
  is skipped if the history entry has any.
- `<(cmd)` temp files were reopened by name after `mkstemp`; they are now written
  through the descriptor `mkstemp` returned and read back with `O_NOFOLLOW`.

Not fuzzed yet (they need real programs or files): redirections, the key-handling
half of the line editor, completion specs that run functions, startup/history
file parsing. Target 11 is a filter, not a container: run long fuzzing sessions
in a throwaway VM or container.

## Still to do

- Branch protection on `main` (require CI, no force pushes) and signed
  commits/tags: repository settings, not code.
- Signed release binaries with provenance (`actions/attest-build-provenance`).
- OpenSSF Best Practices badge (passing level): the questionnaire at
  <https://www.bestpractices.dev/>.
- A SAST pass with `clang-tidy`/`cppcheck` and a triage of the findings.
