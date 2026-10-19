# dash: an unknown -o name is a fatal special-builtin error (pipefail is known: POSIX.1-2024).
set -a; EXPORTED=1; set +a; export | while read a b c; do case "$a $b $c" in *EXPORTED=*) echo exported;; esac; done
( set -n; echo notrun ); echo done
set -o | { n=0; while read name state; do case $name in errexit) n=$((n+1));; esac; done; echo $n; }
set -o nosuchopt-zz
echo not-reached
