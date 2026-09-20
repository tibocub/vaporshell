set -e
true && echo ok1
false || echo ok2
if false; then :; fi; echo ok3
! false; echo ok4
f() { false; echo "not reached"; }
f || echo "ok5 (f in || context)"
(set -e; false; echo no) || echo "ok6 subshell failed"
x=$(false) || echo "ok7"
while false; do :; done; echo ok8
false && :; echo ok9
set +e; false; echo "ok10 after set +e"
set -e
echo before
false
echo "never printed"
