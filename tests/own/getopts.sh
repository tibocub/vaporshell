# getopts: OPTIND/OPTARG handling, clusters, silent mode, resets.
set -- -a -b val x y
while getopts ab: o; do echo "$o=$OPTARG"; done
echo "OPTIND=$OPTIND"
shift $((OPTIND - 1)); echo "rest: $*"

OPTIND=1
set -- -ab -cVALUE
while getopts abc: o; do echo "$o [${OPTARG-unset}]"; done
echo "OPTIND=$OPTIND"

OPTIND=1
set -- -z
getopts a o 2>/dev/null; echo "o=$o OPTARG=[${OPTARG-unset}] rc=$?"
OPTIND=1
getopts :a o; echo "o=$o OPTARG=[$OPTARG]"
OPTIND=1
set -- -b
getopts :b: o; echo "o=$o OPTARG=[$OPTARG]"

OPTIND=1
set -- -a -- -b
while getopts ab o; do echo $o; done; echo "OPTIND=$OPTIND"

OPTIND=1
while getopts xy: o -x -y arg; do echo "$o:${OPTARG-}"; done

f() { OPTIND=1; while getopts n: o; do echo "f: $o=$OPTARG"; done; }
f -n 5
