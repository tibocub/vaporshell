#!/bin/sh
# tests/check-all.sh -- everything, one report.
#
#   sh tests/check-all.sh build/vaporshell build-asan/vaporshell   (see `make check-all`)
#
# Runs, and adds up:
#   1. every differential suite on the normal Linux build   (tests/run-suites.sh)
#   2. the same suites on the ASan+UBSan build
#   3. the smoosh POSIX corpus                               (tests/smoosh-check.sh)
#   4. vaporOS/NuttX: build, symbol check, smoke test and the same suites in
#      the simulator                                         (tests/nuttx-check.sh)
#
# It keeps going after a failure so one run shows everything, and ends with one
# TOTAL line. A step whose prerequisite is missing (no dash, no vaporOS
# workspace) is reported as skipped, not failed.
#
# Options are passed through to nuttx-check.sh (--full, --no-build, --jobs N);
# SKIP_NUTTX=1 leaves the vaporOS step out. See nuttx-check.sh for VAPOROS_DIR,
# NUTTX_DIR, BASH_REF and DASH_REF.

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/lib.sh"

VS=$1
VS_ASAN=$2
if [ $# -ge 2 ]; then shift 2; else shift $#; fi     # (a failed shift ends a POSIX shell)

[ -x "$VS" ] || { echo "usage: $0 path/to/vaporshell path/to/vaporshell-asan [nuttx-check options]" >&2; exit 2; }

VS_TALLY=$(mktemp "${TMPDIR:-/tmp}/vaporshell-tally.XXXXXX") || exit 2
export VS_TALLY
trap 'rm -f "$VS_TALLY"' EXIT INT TERM
rc=0
skipped=

step() { printf '\n%s######## %s%s\n' "$C_BOLD" "$1" "$C_RESET"; }

# Every step is run through the tally guard: see tally_guard in lib.sh.
step "Linux: all suites"
before=$(tally_failed "$VS_TALLY")
VS_PLATFORM=Linux sh "$HERE/run-suites.sh" "$VS"; st=$?
[ "$st" -eq 0 ] || rc=1
tally_guard "$VS_TALLY" "Linux: all suites" "$st" "$before"

if [ -x "$VS_ASAN" ]; then
    step "Linux, ASan+UBSan: all suites"
    before=$(tally_failed "$VS_TALLY")
    VS_PLATFORM="Linux ASan" VS_SKIP_INPROC="${VS_SKIP_INPROC_ASAN:-}" ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" \
        sh "$HERE/run-suites.sh" "$VS_ASAN"; st=$?
    [ "$st" -eq 0 ] || rc=1
    tally_guard "$VS_TALLY" "Linux ASan: all suites" "$st" "$before"
else
    skipped="$skipped\n  ASan build (pass its path as the second argument)"
fi

step "Linux: smoosh POSIX corpus"
before=$(tally_failed "$VS_TALLY")
sh "$HERE/smoosh-check.sh" "$VS"; st=$?
[ "$st" -eq 0 ] || rc=1
tally_guard "$VS_TALLY" "smoosh corpus" "$st" "$before"

if [ -z "$SKIP_NUTTX" ]; then
    step "vaporOS / NuttX"
    before=$(tally_failed "$VS_TALLY")
    sh "$HERE/nuttx-check.sh" "$@"; st=$?
    case $st in
        0) ;;
        3) skipped="$skipped\n  vaporOS/NuttX (no workspace found)" ;;
        *) rc=1
           tally_guard "$VS_TALLY" "vaporOS / NuttX" "$st" "$before" ;;
    esac
else
    skipped="$skipped\n  vaporOS/NuttX (SKIP_NUTTX=1)"
fi

printf '\n%s######## Summary%s\n' "$C_BOLD" "$C_RESET"
tally_report "$VS_TALLY" 1 || rc=1
if [ -n "$skipped" ]; then
    printf '%sNot run:%s%b\n' "$C_YELLOW" "$C_RESET" "$skipped"
fi
exit $rc
