# [[ ]]: strings, patterns, tests, arithmetic operands, && || ! ( ), regex.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
[[ abc == abc ]] && echo eq; [[ abc == abd ]] || echo ne
[[ abcdef == a*f ]] && echo glob; [[ abc == a?c ]] && echo q; [[ abc != x* ]] && echo notx
p="a*"; [[ abc == $p ]] && echo pat-unquoted; [[ abc == "$p" ]] || echo pat-quoted-literal
: > f; mkdir d; [[ -f f ]] && echo f; [[ -d d ]] && echo d; [[ -e nope ]] || echo nope
[[ -z "" ]] && echo z; [[ -n x ]] && echo n; [[ -s f ]] || echo empty
[[ 1 == 1 && 2 == 2 ]] && echo and; [[ 1 == 2 || 2 == 2 ]] && echo or
[[ ! 1 == 2 ]] && echo not; [[ ( 1 == 1 ) && ! ( 2 == 3 ) ]] && echo paren
[[ 3 -lt 10 ]] && echo lt; [[ 10 -ge 10 ]] && echo ge; [[ 1+1 -eq 2 ]] && echo arith
x=5; [[ x -eq 5 ]] && echo bare-name
[[ a < b ]] && echo lexlt; [[ b > a ]] && echo lexgt
x="a b"; [[ $x == "a b" ]] && echo nosplit
y=; [[ $y == "" ]] && echo empty-ok
[[ x ]] && echo single; [[ "" ]] || echo single-empty
x=1; [[ -v x ]] && echo set; [[ -v nope ]] || echo unset
set -e; [[ -o errexit ]] && echo opt-on; set +e; [[ -o errexit ]] || echo opt-off
[[ a == a ]]; echo "rc=$?"; [[ a == b ]]; echo "rc=$?"
[[ 1 == 1 &&
   2 == 2 ]] && echo multi-line
: > older; : > newer; [[ newer -ef newer ]] && echo ef
shopt -u extglob; [[ a == @(a|b) ]] && echo ext-always-on-here
