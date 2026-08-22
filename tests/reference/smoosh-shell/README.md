# smoosh's own POSIX shell test suite (vendored)

Vendored from [mgree/smoosh](https://github.com/mgree/smoosh),
`tests/shell/`, commit `cc67dbe6a4953e51431997eac025b5e3f46c3d2d`
(2023-02-16). MIT-licensed (see `LICENSE` in this directory, copied
from the same commit) -- compatible with this project's own MIT
licensing, unlike bash's own test suite (GPL; see
`fetch-bash-posix2.sh` in the parent directory for why that one is
fetched on demand instead of vendored here).

Smoosh is an academic, executable formalization of the POSIX shell
standard; this test suite is described in its own paper as "a test
suite of our own devising," used to validate Smoosh's semantics
against bash, dash, zsh, mksh, ksh93, and yash. Not the official Open
Group POSIX conformance suite (that one's commercial/licensed, not
freely available) -- but real, focused, POSIX-level shell behavior,
not bash-specific extensions, which is exactly the level vaporshell's
own goals are at right now.

## Format

Each test is `<category>.<name>.test` -- a small, usually just a few
lines, self-contained POSIX shell script. Categories seen here:
`builtin.*` (cd, echo, printf, trap, set, test, ...), `parse.*`,
`semantics.*`, plus a couple of `benchmark.*` timing scripts (not
useful for us, correctness isn't their point). Some tests also have a
matching `.out` (expected stdout) and/or `.err` (expected stderr) file
alongside; others just rely on their own exit code (`[ ... ] || exit
N`-style assertions inside the script itself).

Not modified from upstream -- if a test needs adjusting for vaporshell
specifically, that adjustment belongs in the differential-test harness
(`../run-differential.py`) or in a documented, separate note, not by
editing these files in place.

## What this is for, right now

Primarily a roadmap: reading through the categories here is a
concrete, evidence-based checklist of what a real POSIX shell actually
needs to handle, ordered by how simple the test itself is. Most of
these can't meaningfully run against vaporshell yet, since vaporshell
doesn't read a script file at all yet, only one interactive line at a
time -- see `../run-differential.py`'s own notes on how it handles
(and reports on) that gap honestly rather than silently.
