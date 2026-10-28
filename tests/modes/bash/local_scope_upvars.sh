# local x again keeps its value; unset in a callee reaches the caller's local
f() { local x=1 a=(p q); local x a; echo "$x ${a[*]}"; }
f
x=global
g() { unset -v x; eval x=2; }
h() { local x=1; g; echo "h x=$x"; }
h; echo "after x=$x"
k() { local y=1; l; echo "k y=$y"; }
l() { local y; y=3; unset -v y; eval y=4; echo "l y=$y"; }
k
