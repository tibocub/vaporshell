# local: scoping and restore. (`local x` with no value differs; see modes/.)
v=out
f() { local v=1; echo "in f: $v"; g; }
g() { echo "g sees: $v"; }
f; echo "after: $v"
h() { local a=1 b=2; a=$((a + b)); echo "$a $b"; }
h; echo "a=[${a-unset}] b=[${b-unset}]"
k() { local n=$1; if [ "$n" -gt 0 ]; then k $((n - 1)); fi; echo "n=$n"; }
k 2
export E=outer
m() { local E=inner; echo "m: $E"; }
m; echo "E=$E"
unset z; n() { local z=1; }; n; echo "z=[${z-unset}]"
