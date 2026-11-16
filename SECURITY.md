# Security policy

## Supported versions

Only the latest release (and `main`) gets security fixes. Vaporshell is
pre-1.0: the newest `0.X.Y` is the supported one.

## Reporting a vulnerability

Please do **not** open a public issue. Use GitHub's private reporting:
<https://github.com/tibocub/vaporshell/security/advisories/new>

Include the vaporshell version (`echo $VAPORSHELL_VERSION`), how you built it,
and a minimal input or script that triggers the problem. A crash found by
fuzzing is welcome as it is (the file `libFuzzer` saved is enough).

You should get an answer within 7 days. Confirmed issues are fixed in a new
patch release and credited in its notes unless you prefer otherwise.

## What counts

A shell runs whatever it is told to, so running a script you wrote is not a
vulnerability. These are:

- memory-safety bugs (overflow, use-after-free, out-of-bounds read) reachable
  from script text, command-line input, environment, history files, or
  completion specs;
- undefined behaviour in the parser, expansion, arithmetic, globbing or line
  editor;
- the shell running something it was not asked to (an expansion or a
  completion that executes code, a prompt or title escape that can be
  injected from file names or command text);
- unsafe handling of its own files (history, startup files) such as wrong
  permissions or following a planted symlink.

See `docs/security.md` for what is already done (sanitizer builds, fuzzing,
hardened flags) and how to reproduce it.
