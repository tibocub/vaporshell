#!/bin/bash
# tests/corpus/run.sh -- real-world differential tests: run third-party bash
# code under bash and vaporshell, in a bwrap sandbox, and diff stdout + exit
# status (stderr is saved, and reported as "stderr-diff", not a failure).
#
#   tests/corpus/run.sh path/to/vaporshell [ONLY-substring]
#
# CORPUS=dir holds the cloned projects (default ~/Code/corpus). Cases are
# lines in cases.tsv: name<TAB>command. The command runs under a neutral
# bash with $SH = the shell under test, and with `bash` on PATH pointing at
# that shell too, so `#!/usr/bin/env bash` scripts and re-execs use it.
# Known diffs go in known-diffs.txt (one case name per line). Output of every
# run is kept in $OUT (default /tmp/vaporshell-corpus) for inspection.
VS=$(realpath "$1"); ONLY=$2
HERE=$(cd "$(dirname "$0")" && pwd)
export C=${CORPUS:-$HOME/Code/corpus}
OUT=${OUT:-/tmp/vaporshell-corpus}; rm -rf "$OUT"; mkdir -p "$OUT"
REF=$(command -v bash)
pass=0 fail=0 known=0 sdiff=0
run() { # shell-path outdir cmd
    local sh=$1 d=$2 cmd=$3 w
    w=$(mktemp -d "$OUT/w.XXXXXX"); mkdir -p "$w/bin" "$w/home"
    ln -s "$sh" "$w/bin/bash"
    timeout 60 bwrap --ro-bind / / --dev /dev --proc /proc --tmpfs /tmp --bind "$w" "$w" \
         --unshare-pid --unshare-net --die-with-parent \
        env -i PATH="$w/bin:/usr/bin:/bin" HOME="$w/home" TERM=dumb LANG=C.UTF-8 \
        C="$C" SH="$sh" W="$w" "$REF" -c "$cmd" >"$d.out" 2>"$d.err" </dev/null
    echo $? >"$d.rc"
    sed -i "s#$w#WORK#g; s#$sh#SH#g" "$d.out" "$d.err"
}
while IFS=$'\t' read -r name cmd; do
    case $name in ''|'#'*) continue;; esac
    [[ -z $ONLY || $name == *$ONLY* ]] || continue
    run "$REF" "$OUT/$name.ref" "$cmd"; run "$VS" "$OUT/$name.vs" "$cmd"
    if cmp -s "$OUT/$name.ref.out" "$OUT/$name.vs.out" && cmp -s "$OUT/$name.ref.rc" "$OUT/$name.vs.rc"; then
        pass=$((pass+1)); st=ok
        cmp -s "$OUT/$name.ref.err" "$OUT/$name.vs.err" || { sdiff=$((sdiff+1)); st="ok (stderr-diff)"; }
    elif grep -qxF "$name" "$HERE/known-diffs.txt" 2>/dev/null; then known=$((known+1)); st=known-diff
    else fail=$((fail+1)); st=FAIL; fi
    printf '%-28s %s\n' "$name" "$st"
done <"$HERE/cases.tsv"
echo "corpus: $pass ok ($sdiff with stderr diffs), $known known-diff, $fail FAIL  (outputs in $OUT)"
[ $fail -eq 0 ]
