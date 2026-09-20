# set -o pipefail / allexport / noexec, set -o listing.
false | true; echo "plain: $?"
set -o pipefail; false | true; echo "pipefail: $?"
true | false | true; echo "middle: $?"
set +o pipefail; false | true; echo "off: $?"
set -a; EXPORTED=1; set +a; sh -c 'echo ${EXPORTED-unset}'
( set -n; echo notrun ); echo done
set -o | grep -c 'errexit' | tr -d ' '
set +o | grep -c 'set +o errexit' | tr -d ' '
set -o noclobber; set +o noclobber; echo ok
