#!/bin/sh
# tests/check-all.sh -- every differential suite, each against the shell
# that defines the behaviour:
#
#   own/           common ground: vaporshell vs bash, vaporshell --posix vs dash
#   modes/bash/    bash mode:     vaporshell          vs bash
#   modes/posix/   POSIX mode:    vaporshell --posix  vs dash
#
# POSIX mode is modelled on dash (0.5.12), not on bash --posix: the two
# disagree (docs/modes.md), and dash is the strict one.
#
#   sh tests/check-all.sh path/to/vaporshell
#
# BASH=/path/to/bash selects the bash-mode reference (default: bash on PATH),
# DASH likewise. Which versions ran is printed first: differences between
# bash releases show up as failures here, and in docs/bash-coverage.md.

VS=$1
HERE=$(dirname "$0")
BASH=${BASH:-bash}
DASH=${DASH:-dash}
rc=0

[ -x "$VS" ] || { echo "usage: $0 path/to/vaporshell" >&2; exit 2; }

echo "bash-mode reference:  $($BASH --version 2>/dev/null | head -n 1)"
if command -v "$DASH" >/dev/null 2>&1; then
    echo "posix-mode reference: $DASH ($(dpkg -s dash 2>/dev/null | sed -n 's/^Version: //p'))"
else
    echo "posix-mode reference: (dash not installed: POSIX suites skipped)"
fi

sh "$HERE/posix-check.sh" "$VS" "$BASH" own || rc=1
sh "$HERE/posix-check.sh" "$VS" "$BASH" modes/bash || rc=1
if command -v "$DASH" >/dev/null 2>&1; then
    sh "$HERE/posix-check.sh" "$VS" "$DASH" own --posix || rc=1
    sh "$HERE/posix-check.sh" "$VS" "$DASH" modes/posix --posix || rc=1
fi

exit $rc
