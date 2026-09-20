#!/bin/sh
# tests/run-suites.sh -- every differential suite, each against the shell
# that defines the behaviour:
#
#   own/           common ground: vaporshell vs bash, vaporshell --posix vs dash
#   modes/bash/    bash mode:     vaporshell          vs bash
#   modes/posix/   POSIX mode:    vaporshell --posix  vs dash
#
# POSIX mode is modelled on dash (0.5.12), not on bash --posix: the two
# disagree (docs/modes.md), and dash is the strict one.
#
#   sh tests/run-suites.sh path/to/vaporshell
#
# BASH_REF=/path/to/bash selects the bash-mode reference (default: bash on
# PATH), DASH_REF likewise. (Not $BASH: bash sets that variable itself, to the
# path it was started as, so on systems where sh is bash it would silently
# become `bash --posix`.) Which versions ran is printed first: differences
# between bash releases show up as failures here and in docs/bash-coverage.md.

. "$(dirname "$0")/lib.sh"

VS=$1
HERE=$(dirname "$0")
BASH_REF=${BASH_REF:-bash}
DASH_REF=${DASH_REF:-dash}
rc=0

# Standalone: keep our own tally so the run ends with one TOTAL line. Under
# a wrapper (tests/check-all.sh) VS_TALLY is already set and it does the total.
if [ -z "$VS_TALLY" ]; then
    VS_TALLY=$(mktemp "${TMPDIR:-/tmp}/vaporshell-tally.XXXXXX") || exit 2
    export VS_TALLY
    own_tally=1
    trap 'rm -f "$VS_TALLY"' EXIT INT TERM
fi

[ -x "$VS" ] || { echo "usage: $0 path/to/vaporshell" >&2; exit 2; }

# The bash reference must really be bash in its default mode; a reference that
# is something else would make every bash-mode result meaningless.
bver=$("$BASH_REF" --version 2>/dev/null | head -n 1)
case $bver in
    "GNU bash"*) ;;
    *) echo "BASH_REF ($BASH_REF) is not GNU bash; set BASH_REF=/path/to/bash" >&2; exit 2 ;;
esac
# ...and not bash started as `sh`, which is bash in POSIX mode.
if ! "$BASH_REF" -c 'case $SHELLOPTS in *posix*) exit 1;; esac' 2>/dev/null; then
    echo "BASH_REF ($BASH_REF) runs in POSIX mode (started as sh?); use the real bash binary" >&2
    exit 2
fi
echo "bash-mode reference:  $bver"
if command -v "$DASH_REF" >/dev/null 2>&1; then
    dver=$(dpkg -s dash 2>/dev/null | sed -n 's/^Version: //p')
    [ -n "$dver" ] || dver=$(rpm -q dash 2>/dev/null)
    echo "posix-mode reference: $DASH_REF (${dver:-version unknown})"
else
    echo "posix-mode reference: (dash not installed: POSIX suites skipped)"
fi

sh "$HERE/posix-check.sh" "$VS" "$BASH_REF" own || rc=1
sh "$HERE/posix-check.sh" "$VS" "$BASH_REF" modes/bash || rc=1
if command -v "$DASH_REF" >/dev/null 2>&1; then
    sh "$HERE/posix-check.sh" "$VS" "$DASH_REF" own --posix || rc=1
    sh "$HERE/posix-check.sh" "$VS" "$DASH_REF" modes/posix --posix || rc=1
fi

# The same suites again with subshells and $(...) run in-process, the way
# they must on NuttX (which has no fork). VS_INPROC is a test hook; see
# inproc.c. Skipped if the caller already set it, or with VS_SKIP_INPROC=1.
if [ -z "$VS_INPROC" ] && [ -z "$VS_SKIP_INPROC" ]; then
    printf '%s-- in-process subshells (VS_INPROC=1) --%s\n' "$C_BOLD" "$C_RESET"
    VS_INPROC=1 sh "$HERE/posix-check.sh" "$VS" "$BASH_REF" own || rc=1
    VS_INPROC=1 sh "$HERE/posix-check.sh" "$VS" "$BASH_REF" modes/bash || rc=1
    if command -v "$DASH_REF" >/dev/null 2>&1; then
        VS_INPROC=1 sh "$HERE/posix-check.sh" "$VS" "$DASH_REF" own --posix || rc=1
        VS_INPROC=1 sh "$HERE/posix-check.sh" "$VS" "$DASH_REF" modes/posix --posix || rc=1
    fi
fi

if [ -n "$own_tally" ]; then
    echo
    tally_report "$VS_TALLY" 1 || rc=1
fi

exit $rc
