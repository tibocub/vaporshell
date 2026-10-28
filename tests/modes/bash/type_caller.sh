type -P ls >/dev/null; echo "P rc=$?"
type -P nosuchcmd-xyz; echo "P missing rc=$?"
type -- cd
type -t -- cd
type -P -- cd >/dev/null; echo "rc=$?"
g() { caller 0; echo "rc=$?"; caller 1; echo "rc=$?"; caller 2; echo "rc=$?"; }
f() { g; }
f
