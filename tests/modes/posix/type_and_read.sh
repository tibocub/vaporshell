# dash: type/command -v report 127, read -p exists, umask -S, shift is fatal.
type nosuchcmd_zz; echo "rc=$?"
command -v nosuchcmd_zz; echo "rc=$?"
echo in | { read -p '' x; echo "$x"; }
umask 022; umask -S; umask u=rwx,g=rx,o=; umask; umask 022
export ZED=1; export | grep -c '^export ZED=' | tr -d ' '
set -- a
shift 2
echo not-reached
