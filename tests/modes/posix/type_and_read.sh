# dash: type/command -v report 127, read -p exists, umask -S, shift is fatal.
type nosuchcmd_zz; echo "rc=$?"
command -v nosuchcmd_zz; echo "rc=$?"
echo in | { read -p '' x; echo "$x"; }
umask 022; umask -S; umask u=rwx,g=rx,o=; umask; umask 022
export ZED=1; export | { n=0; while read a b; do case $b in ZED=*) n=$((n+1));; esac; done; echo $n; }
set -- a
shift 2
echo not-reached
