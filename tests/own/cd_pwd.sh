# requires: cmd:ln
# cd/pwd: logical paths (POSIX default) versus physical.
mkdir -p real/sub && ln -s real link
cd link/sub; pwd; pwd -P; pwd -L
cd ..; pwd
cd -P ..; pwd
cd "$OLDPWD" 2>/dev/null; pwd
cd /; cd - >/dev/null; pwd
CDPATH=/tmp; cd / ; cd .. ; pwd
cd . ; pwd
cd real/../real/sub; pwd
echo "PWD=$PWD"
cd /nonexistent-dir 2>/dev/null; echo "rc=$?"
