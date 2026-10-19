# getopts cases where dash 0.5.13 differs from POSIX and bash (the reference here is bash):
# silent mode sets OPTARG to the unknown option character, and OPTIND=1 resets the parse
# even after a loop that ended on `--`.
OPTIND=1
set -- -z
getopts :a o; echo "o=$o OPTARG=[$OPTARG]"

set -- -a -- -b
while getopts ab o; do echo $o; done; echo "OPTIND=$OPTIND"
OPTIND=1
while getopts xy: o -x -y arg; do echo "$o:${OPTARG-}"; done
