# requires: cmd:sed cmd:tr cmd:tail wc-format
# Large and mixed pipelines: streams bigger than a pipe buffer through functions,
# builtins, compound commands and externals; subshell semantics of each stage.
seq 1 5000 | wc -l | tr -d ' '
f() { i=0; while [ $i -lt 3000 ]; do echo "row $i"; i=$((i+1)); done; }
f | wc -l | tr -d ' '
f | tail -n 1
f | head -n 2
f | sed -n '2p;3000p'
echo hello | { read a; echo "got: $a"; }
echo "a b c" | while read x y z; do echo "$z $y $x"; done
{ echo one; echo two; } | cat | { read p; read q; echo "$p+$q"; }
for i in 1 2 3; do echo $i; done | sort -r | tr '\n' ' '; echo
printf 'x\ny\n' | { cat; echo done; } | tr a-z A-Z
false | true; echo "rc=$?"
true | false; echo "rc=$?"
echo data | cat | cat | cat | wc -c | tr -d ' '
x=1; echo z | { x=2; cat >/dev/null; }; echo "x still $x"
echo end
set -o pipefail 2>/dev/null && { false | true; echo "pipefail=$?"; set +o pipefail; }
f | { head -n 1; cat >/dev/null; }
