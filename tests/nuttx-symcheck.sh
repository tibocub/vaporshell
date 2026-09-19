#!/bin/sh
# tests/nuttx-symcheck.sh -- find global symbols vaporshell defines that
# something else in the NuttX link also defines.
#
# Everything in libapps.a (and the kernel/libc archives) shares one flat
# symbol namespace, so a plain name like g_builtins can collide with a
# NuttX or toybox global and only fail at the very last link step. The
# linker reports just the members it happens to pull in; this checks all
# of them.
#
#   sh tests/nuttx-symcheck.sh ../nuttx/staging [member-pattern]
#
# member-pattern (default "vaporshell") selects vaporshell's own object
# files inside libapps.a; NuttX puts the source directory in their names.
# Exit status 1 if any collision is found.

STAGING=$1
PAT=${2:-vaporshell}

if [ -z "$STAGING" ] || [ ! -f "$STAGING/libapps.a" ]; then
    echo "usage: $0 path/to/nuttx/staging [member-pattern]" >&2
    exit 2
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/vaporshell-sym.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT INT TERM

defined() { nm -A -g --defined-only "$@" 2>/dev/null |
            awk '$2 ~ /^[TDBRVWC]$/ { print $3 }'; }

mkdir "$TMP/objs"
members=$(ar t "$STAGING/libapps.a" | grep "$PAT")
if [ -z "$members" ]; then
    echo "no members matching '$PAT' in libapps.a" >&2
    exit 2
fi

(cd "$TMP/objs" && for m in $members; do ar x "$STAGING/libapps.a" "$m"; done)
defined "$TMP"/objs/*.o | sort -u > "$TMP/mine"

{
    for a in "$STAGING"/*.a; do
        if [ "$(basename "$a")" = libapps.a ]; then
            nm -A -g --defined-only "$a" 2>/dev/null | grep -v "$PAT" |
                awk '$2 ~ /^[TDBRVWC]$/ { print $3 }'
        else
            defined "$a"
        fi
    done
} | sort -u > "$TMP/others"

collisions=$(comm -12 "$TMP/mine" "$TMP/others")
echo "$(wc -l < "$TMP/mine") vaporshell globals checked against $(wc -l < "$TMP/others") others"

if [ -n "$collisions" ]; then
    echo "COLLISIONS (rename these, e.g. with a vs_/g_vs_ prefix):"
    echo "$collisions" | sed 's/^/  /'
    exit 1
fi

echo "no collisions"
