f() { echo hi; }
declare -f -- f >/dev/null; echo rc=$?
declare -f nosuch >/dev/null; echo rc=$?
eval "$(declare -f f)"; f
compgen -A helptopic -- ech
compgen -A setopt -- pipef
compgen -A shopt -- extg
compgen -A signal -- SIGINT
compgen -A signal -- EXI
a=(x "y z"); compgen -W '"${a[@]}" -{1..2}' --
compgen -d -V r; echo "${#r[@]}"
