#!/usr/bin/env bash
# tests/reference/fetch-bash-posix2.sh -- fetches bash's own
# tests/posix2.tests on demand, into this same directory. Never
# committed (see .gitignore right next to this script) -- posix2.tests
# is GPL-licensed (part of bash's own test suite), and this project is
# MIT, the same reason toybox/vaporOS-coreutils was chosen partly for
# its own permissive license in the first place. Fetching it fresh
# each time, rather than vendoring a copy, keeps that boundary real
# instead of just documented.
#
# Small, focused, genuinely POSIX-level test (206 lines) -- parameter
# expansion, IFS splitting, positional parameters, getopts, arithmetic
# expansion -- not bash's own, much larger suite of bash-specific
# extensions (arrays, [[, shopt, ...), which isn't a useful near-term
# roadmap for vaporshell.
#
# Not pinned to a specific commit -- a plain shallow clone of whatever
# the current default branch is, which this file itself hasn't needed
# in practice: its own last-changed comment (inside the file) reads
# "Wed Jun 19 12:24:24 EDT 1996". Low drift risk, and a shallow clone
# of a specific historical commit isn't reliably supported by every
# git server anyway (confirmed directly: a --filter=blob:none
# --no-checkout attempt against this same server just hung).
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$DIR/bash-posix2.tests"

if [ -f "$OUT" ]; then
  echo "$OUT already present -- delete it first to re-fetch."
  exit 0
fi

TMPDIR_FETCH=$(mktemp -d)
trap 'rm -rf "$TMPDIR_FETCH"' EXIT

git clone --depth 1 https://git.savannah.gnu.org/git/bash.git "$TMPDIR_FETCH" >/dev/null 2>&1
cp "$TMPDIR_FETCH/tests/posix2.tests" "$OUT"

echo "Fetched $OUT (bash, current default branch, GPL-licensed, not tracked by git here)."
