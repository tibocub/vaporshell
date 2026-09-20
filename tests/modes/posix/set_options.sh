# dash: no pipefail; an unknown -o name is a fatal special-builtin error.
set -a; EXPORTED=1; set +a; sh -c 'echo ${EXPORTED-unset}'
( set -n; echo notrun ); echo done
set -o | grep -c 'errexit' | tr -d ' '
set -o pipefail
echo not-reached
