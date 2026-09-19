#!/bin/sh
# tests/posix-check.sh -- run every tests/own/*.sh through a reference
# shell and through vaporshell, and compare stdout and exit status.
# stderr is deliberately not compared: error message wording is
# shell-specific and isn't what these scripts are testing.
#
#   sh tests/posix-check.sh path/to/vaporshell [reference-shell]
#
# Each script runs in its own throwaway directory, since some tests
# create files as a side effect (see tests/reference/README.md).
# vaporshell is deliberately NOT put on $PATH: command substitution
# has to find itself without help.

VS=$1
REF=${2:-bash}

if [ -z "$VS" ] || [ ! -x "$VS" ]; then
    echo "usage: $0 path/to/vaporshell [reference-shell]" >&2
    exit 2
fi

VS=$(cd "$(dirname "$VS")" && pwd)/$(basename "$VS")
TESTS=$(cd "$(dirname "$0")" && pwd)/own
TMP=$(mktemp -d "${TMPDIR:-/tmp}/vaporshell-check.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT INT TERM

pass=0
fail=0

for t in "$TESTS"/*.sh; do
    name=$(basename "$t")

    (cd "$TMP" && "$REF" "$t" >"$TMP/ref.out" 2>/dev/null </dev/null)
    ref_rc=$?
    (cd "$TMP" && "$VS" "$t" >"$TMP/vs.out" 2>/dev/null </dev/null)
    vs_rc=$?

    if cmp -s "$TMP/ref.out" "$TMP/vs.out" && [ "$ref_rc" = "$vs_rc" ]; then
        pass=$((pass + 1))
        echo "ok    $name"
    else
        fail=$((fail + 1))
        echo "FAIL  $name (status: $REF=$ref_rc vaporshell=$vs_rc)"
        diff "$TMP/ref.out" "$TMP/vs.out" | head -10
    fi
done

echo "$pass passed, $fail failed (reference: $REF)"
[ "$fail" = 0 ]
