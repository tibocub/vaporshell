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
