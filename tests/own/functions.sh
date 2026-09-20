# requires: cmdsub-state
greet() { echo "hello $1 ($# args)"; }
greet world
greet a b c
add() { echo $(($1 + $2)); }
add 2 3
r() { return 7; }
r; echo "status $?"
fact() { if [ "$1" -le 1 ]; then echo 1; else echo $(( $1 * $(fact $(($1 - 1))) )); fi; }
fact 5
set -- outer1 outer2
show() { echo "inner: $1 $2"; }
show in1 in2
echo "outer: $1 $2"
f() { g() { echo nested; }; g; }
f; g
unset -f g; type g >/dev/null 2>&1 || echo "g gone"
args() { for a in "$@"; do echo "[$a]"; done; }
args "a b" c "" d
x=global; setx() { x=changed; }; setx; echo $x
cnt() { echo $#; }; cnt; cnt "" ""; cnt a "b c"
