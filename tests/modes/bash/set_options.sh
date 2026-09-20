# set -o pipefail / allexport / noexec, set -o listing.
false | true; echo "plain: $?"
set -o pipefail; false | true; echo "pipefail: $?"
true | false | true; echo "middle: $?"
set +o pipefail; false | true; echo "off: $?"
set -a; EXPORTED=1; set +a; export | while read a b c; do case "$a $b $c" in *EXPORTED=*) echo exported;; esac; done
( set -n; echo notrun ); echo done
set -o | { n=0; while read name state; do case $name in errexit) n=$((n+1));; esac; done; echo $n; }
set +o | { n=0; while read a b c; do case "$a $b $c" in "set +o errexit") n=$((n+1));; esac; done; echo $n; }
set -o noclobber; set +o noclobber; echo ok
