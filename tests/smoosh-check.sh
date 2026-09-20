#!/bin/sh
# tests/smoosh-check.sh -- run the vendored smoosh POSIX corpus against a
# vaporshell binary and compare with tests/smoosh-known-failures.txt, so
# progress is a number and regressions are named.
#
#   sh tests/smoosh-check.sh path/to/vaporshell           # report
#   sh tests/smoosh-check.sh path/to/vaporshell --update  # rewrite the list
#
# Runs vaporshell in POSIX mode (--posix): the corpus tests POSIX.
# A test passes if its stdout equals its .out file; tests without a .out
# pass on exit status 0. Rough, but stable enough to track. Each test runs
# in its own throwaway directory (several create files). Exit status is
# non-zero only if something that used to pass now fails.

. "$(dirname "$0")/lib.sh"

VS=$1
MODE=$2

if [ -z "$VS" ] || [ ! -x "$VS" ]; then
    echo "usage: $0 path/to/vaporshell [--update]" >&2
    exit 2
fi

VS=$(cd "$(dirname "$VS")" && pwd)/$(basename "$VS")
HERE=$(cd "$(dirname "$0")" && pwd)
DIR=$HERE/reference/smoosh-shell
KNOWN=$HERE/smoosh-known-failures.txt
TMP=$(mktemp -d "${TMPDIR:-/tmp}/vaporshell-smoosh.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT INT TERM

TIMEOUT=
command -v timeout >/dev/null 2>&1 && TIMEOUT="timeout 10"

FAILS=$TMP/fails.txt
: > "$FAILS"
total=0
passed=0

for t in "$DIR"/*.test; do
    name=$(basename "$t" .test)
    case $name in benchmark.*) continue ;; esac
    total=$((total + 1))

    work=$TMP/work
    rm -rf "$work"; mkdir "$work"
    out=$(cd "$work" && TEST_SHELL="$VS --posix" $TIMEOUT "$VS" --posix "$t" 2>/dev/null </dev/null)
    rc=$?

    ok=0
    if [ -f "$DIR/$name.out" ]; then
        [ "$out" = "$(cat "$DIR/$name.out")" ] && ok=1
    else
        [ "$rc" = 0 ] && ok=1
    fi

    if [ "$ok" = 1 ]; then
        passed=$((passed + 1))
    else
        echo "$name" >> "$FAILS"
    fi
done

sort "$FAILS" > "$TMP/fails.sorted"
echo "smoosh: $passed / $total passed"

if [ "$MODE" = "--update" ]; then
    cp "$TMP/fails.sorted" "$KNOWN"
    echo "wrote $KNOWN"
    exit 0
fi

[ -f "$KNOWN" ] || : > "$KNOWN"
sort "$KNOWN" > "$TMP/known.sorted"

regressions=$(comm -23 "$TMP/fails.sorted" "$TMP/known.sorted")
fixed=$(comm -13 "$TMP/fails.sorted" "$TMP/known.sorted")

# For tests/check-all.sh: passes, regressions (the only real failures), and
# the tests already on the known-failures list (counted as skipped).
nreg=$(printf '%s' "$regressions" | grep -c .)
nknown=$(comm -12 "$TMP/fails.sorted" "$TMP/known.sorted" | grep -c .)
tally_group "smoosh corpus (--posix)" "$passed" "$nreg" "$nknown"
for t in $regressions; do tally_fail "smoosh corpus (--posix)" "$t"; done

if [ -n "$fixed" ]; then
    echo "newly passing (remove from the known list with --update):"
    echo "$fixed" | sed 's/^/  /'
fi

if [ -n "$regressions" ]; then
    printf '%sREGRESSIONS (passed before, fail now):%s\n' "$C_RED" "$C_RESET"
    echo "$regressions" | sed 's/^/  /'
    exit 1
fi

exit 0
