#!/bin/sh
# tests/check-all.sh -- every differential suite, each against the shell
# that defines the behaviour:
#
#   own/           common ground: vaporshell vs bash, in both modes
#   modes/bash/    bash's default:  vaporshell         vs bash
#   modes/posix/   POSIX mode:      vaporshell --posix vs bash --posix
#   modes/posix-strict/  where POSIX is stricter than bash --posix:
#                                   vaporshell --posix vs dash (if present)
#
#   sh tests/check-all.sh path/to/vaporshell

VS=$1
HERE=$(dirname "$0")
rc=0

[ -x "$VS" ] || { echo "usage: $0 path/to/vaporshell" >&2; exit 2; }

sh "$HERE/posix-check.sh" "$VS" bash own || rc=1
sh "$HERE/posix-check.sh" "$VS" "bash --posix" own --posix || rc=1
sh "$HERE/posix-check.sh" "$VS" bash modes/bash || rc=1
sh "$HERE/posix-check.sh" "$VS" "bash --posix" modes/posix --posix || rc=1
if command -v dash >/dev/null 2>&1; then
    sh "$HERE/posix-check.sh" "$VS" dash modes/posix-strict --posix || rc=1
else
    echo "(dash not installed: skipping modes/posix-strict)"
fi

exit $rc
