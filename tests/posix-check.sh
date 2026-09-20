#!/bin/sh
# tests/posix-check.sh -- run every tests/own/*.sh through a reference
# shell and through vaporshell, and compare stdout and exit status.
# stderr is deliberately not compared: error message wording is
# shell-specific and isn't what these scripts are testing.
#
#   sh tests/posix-check.sh path/to/vaporshell [reference [dir [vs-options]]]
#
# reference may carry arguments ("bash --posix"); dir defaults to tests/own;
# vs-options are passed to vaporshell before the script (e.g. --posix).
#
# ONLY=substring runs just the tests whose file name contains it
# (`make check ONLY=printf`). Results are also appended to $VS_TALLY when it
# is set (see lib.sh). Each script runs in its own throwaway directory
# ($WORK, recreated per run), since some tests create files as a side effect.
# vaporshell is deliberately NOT put on $PATH: command substitution
# has to find itself without help.

. "$(dirname "$0")/lib.sh"

VS=$1
REF=${2:-bash}
DIR=${3:-own}
VSOPTS=$4

if [ -z "$VS" ] || [ ! -x "$VS" ]; then
    echo "usage: $0 path/to/vaporshell [reference-shell]" >&2
    exit 2
fi

VS=$(cd "$(dirname "$VS")" && pwd)/$(basename "$VS")
TESTS=$(cd "$(dirname "$0")" && pwd)/$DIR
TMP=$(mktemp -d "${TMPDIR:-/tmp}/vaporshell-check.XXXXXX") || exit 2
WORK=$TMP/work
trap 'rm -rf "$TMP"' EXIT INT TERM

# What this group is called in the summary: suite, reference, vaporshell mode.
LABEL="${VS_PLATFORM:+$VS_PLATFORM: }$DIR ($(basename "${REF%% *}")${VSOPTS:+ $VSOPTS}${VS_INPROC:+, in-process})"

pass=0
fail=0

for t in "$TESTS"/*.sh; do
    name=$(basename "$t")
    case $name in *"$ONLY"*) ;; *) continue ;; esac

    (rm -rf "$WORK" && mkdir "$WORK" && cd "$WORK" && $REF "$t" >"$TMP/ref.out" 2>/dev/null </dev/null)
    ref_rc=$?
    (rm -rf "$WORK" && mkdir "$WORK" && cd "$WORK" && "$VS" $VSOPTS "$t" >"$TMP/vs.out" 2>/dev/null </dev/null)
    vs_rc=$?

    if cmp -s "$TMP/ref.out" "$TMP/vs.out" && [ "$ref_rc" = "$vs_rc" ]; then
        pass=$((pass + 1))
        printf '%sok%s    %s\n' "$C_GREEN" "$C_RESET" "$name"
    else
        fail=$((fail + 1))
        printf '%sFAIL%s  %s (status: %s=%s vaporshell=%s)\n' "$C_RED" "$C_RESET" "$name" "$REF" "$ref_rc" "$vs_rc"
        diff "$TMP/ref.out" "$TMP/vs.out" | head -10
        tally_fail "$LABEL" "$name"
    fi
done

if [ "$fail" = 0 ]; then color=$C_GREEN; else color=$C_RED; fi
printf '%s%d passed, %d failed  [%s]%s\n' "$color" "$pass" "$fail" "$LABEL" "$C_RESET"
tally_group "$LABEL" "$pass" "$fail"
[ "$fail" = 0 ]
