# set -x: PS4, nesting depth, quoting, and the compound commands bash traces
exec 2>&1
x=1; y=abc; s="a b"
set -x
echo "a b" 'c d' "" 'it'"'"'s'
x=2
z="a b" w=
FOO=bar true
(( x + 1 ))
[[ $y == a* && -n $x ]]
[[ ! -d /nonexistent ]]
[[ 3 -lt 4 ]]
[[ $y =~ ^a.c$ ]]
for i in $s "$s"; do :; done
case $y in a*) : ;; esac
echo $(echo sub)
eval 'echo ev'
for ((j=0; j<1; j++)); do :; done
PS4='[$LINENO] '
echo lineno
PS4='>> '
echo changed
set +x
echo "$SHELLOPTS"
echo "${PS4}"
