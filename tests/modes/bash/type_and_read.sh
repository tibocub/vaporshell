# type -t, read -n/-d/-t, let, builtin, umask -S, export listing, return.
type -t echo; type -t if; type -t nosuchcmd_zz; echo "rc=$?"
echo abc | { read -n 2 x; echo "$x"; }
printf 'a,b' | { read -d , x; echo "$x"; }
read -t 0 x </dev/null; echo "rc=$?"
let "v = 2 + 3"; echo $v; let v-=5; echo "rc=$?"
echo() { printf 'func\n'; }; builtin echo real; unset -f echo
umask 022; umask -S; umask u=rwx,g=rx,o=; umask; umask 022
export ZED=1 ALPHA=2; export | while read a b c; do case $c in ALPHA=*|ZED=*) echo "$a $b $c";; esac; done
return 2>/dev/null; echo "return outside: $?"
f() { return 3; }; f; echo $?
