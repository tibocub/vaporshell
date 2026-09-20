# Reference test corpus

Real, external POSIX shell test material, used two ways: as a
concrete, evidence-based roadmap of what a real shell needs to handle
(most useful right now, since vaporshell doesn't read a script file
yet), and eventually for differential testing (`run-differential.py`)
-- run the same script through bash, dash, and vaporshell, compare
output.

Two sources, handled differently because of licensing:

- **`smoosh-shell/`** -- vendored directly (MIT-licensed, matches this
  project's own license). ~150 small, focused POSIX shell tests from
  the Smoosh project's own test suite.
- **`bash-posix2.tests`** -- fetched on demand by
  `fetch-bash-posix2.sh`, never committed (GPL-licensed, part of
  bash's own test suite -- see that script's own comment for why that
  matters here). One file, 206 lines, genuinely POSIX-level (parameter
  expansion, IFS splitting, positional parameters, getopts, arithmetic
  expansion).

Neither is the official Open Group POSIX conformance suite
(commercial/licensed, not freely available) -- both are real, freely
available, and focused on actual POSIX-level shell behavior rather
than any one shell's own extensions, which is the right level for
where vaporshell is now.

See each subdirectory/file's own notes for more.

## Running the differential tests on NuttX

`run-differential.py` runs the same script through bash, dash and vaporshell
(inside the NuttX simulator) and compares. Normally you use it through
`make -f posix.mk check-vaporos` (which also rebuilds), but directly:

```
python3 tests/reference/run-differential.py --all-suites --brief --nuttx-dir ../nuttx
python3 tests/reference/run-differential.py --all-suites --suite modes-posix --jobs 4 --nuttx-dir ../nuttx
python3 tests/reference/run-differential.py --nuttx-dir ../nuttx tests/own/printf.sh   # one test, full output
```

`--all-suites` runs the four suites `tests/run-suites.sh` runs (each against
its reference shell); `--brief` prints one line per test and full output only
for failures; `--jobs N` runs N simulators at once; `--only` filters by file
name; the exit status is 1 if anything differs. The test's stderr is dropped
(as on the native side) and its stdout is taken from between two markers the
shell itself prints, so console echo and prompts never leak into a comparison.
A test that needs something NuttX lacks says so in a `# requires:` line near
its top (the tokens are `NUTTX_LACKS` in the script) and is reported as
skipped.

## Locale

The reference shells run with `LC_ALL=C` and no other locale variable (not even `LANG`, which they would fall back to after a script unsets `LC_ALL`). The shell under test in this runner is
the NuttX one, which has no locales and orders glob results and `[[ a < b ]]` by
bytes; a reference running in the caller's locale (en_US.UTF-8 puts `a.txt`
before `B.txt`) could not agree with it. The host suites (`tests/posix-check.sh`)
do the opposite on purpose: both shells inherit the caller's locale, and
vaporshell follows it with `strcoll` as bash does.
