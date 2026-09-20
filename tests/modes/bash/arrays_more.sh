# Indexed arrays: unset on elements, [[ -v ]], subscripts in arithmetic, `set` listing, ${@} forms.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
# unset elem
( c=(1 2 3 4); unset "c[1]"; echo ${!c[@]} ${#c[@]}; unset "c[-1]"; echo ${!c[@]}; unset "c[9]"; echo rc=$?
)
# unset all
( a=(1 2 3); unset "a[@]"; echo "n=${#a[@]}"; a+=(x); echo ${!a[@]}; b=(1 2); unset "b[*]"; echo ${#b[@]}
)
# unset var
( a=(1 2); unset a; echo "[${a[@]}]"; a=(1 2); unset a[0]; echo ${a[@]}; echo ${!a[@]}
)
# unset scalar elem
( s=v; unset "s[0]"; echo "[$s]"; t=v; unset "t[1]" 2>/dev/null; echo "rc=$? [$t]"
)
# unset expr idx
( a=(1 2 3 4); i=2; unset "a[i]"; echo ${a[@]}; unset "a[i-1]"; echo ${a[@]}
)
# slice sparse
( a=(a b c d e); unset "a[1]"; echo ${a[@]:1:2}; echo ${a[@]:2}; echo "${a[@]: -2}"; echo ${#a[@]}
)
# sparse iteration
( a=(a b c d); unset "a[1]" "a[2]"; for i in "${!a[@]}"; do echo $i:${a[i]}; done
)
# -v
( d=(x y); [[ -v d ]] && echo v-d; [[ -v d[1] ]] && echo v-d1; [[ -v d[5] ]] || echo no-d5; [[ -v d[@] ]] && echo v-all; e=(); [[ -v e ]] || echo e-unset; [[ -v e[@] ]] || echo e-all-unset; f=([1]=z); [[ -v f ]] || echo f0-unset; [[ -v f[1] ]] && echo f1-set; [[ -v nope[0] ]] || echo nope
)
# -v expr
( a=(x y z); i=2; [[ -v a[i] ]] && echo yes; [[ -v a[i+1] ]] || echo no
)
# test -v
( a=(x); test -v a && echo t-a; test -v a[0] && echo t-a0; [ -v a[3] ] || echo n3
)
# arith read
( a=(10 20 30); echo $((a[1] + a[2])) $((a[0]*2)) $(( a[1+1] )) $((a[-1]))
)
# arith assign
( a=(1 2 3); (( a[1] = 7 )); echo ${a[@]}; (( a[2] += 5 )); echo ${a[@]}; (( a[5] = 1 )); echo ${!a[@]}; echo $(( a[0] = a[1] * 2 )) ${a[0]}
)
# arith incdec
( a=(1 2 3); (( a[0]++ )); (( ++a[1] )); (( a[2]-- )); echo ${a[@]}; echo $(( a[0]++ + 1 )) ${a[0]}
)
# arith side effect once
( a=(1 2 3 4); i=0; echo $(( a[i++] )) $i; echo $(( a[i++] + 0 )) $i; (( a[i++] = 9 )); echo $i ${a[@]}
)
# arith sc
( a=(0 5); i=0; echo $(( a[0] && a[i++] )) $i; echo $(( a[1] || a[i++] )) $i
)
# arith nested
( a=(2 0 1); b=(1 2); echo $(( a[b[0]] + a[b[1]] )) ${a[b[1]]} ${a[a[0]]}
)
# arith unset elem
( a=(1); echo $(( a[3] + 1 )); a[2]=; echo $(( a[2] + 1 ))
)
# arith elem expr
( a=(1+1 2*3); echo $(( a[0] * a[1] ))
)
# arith for
( a=(); for ((i=0;i<4;i++)); do a[i]=$((i*i)); done; echo ${a[@]}
)
# arith assign scalar unchanged
( x=5; (( x += 2 )); echo $x; (( y = x * 2 )); echo $y; (( z[1] = 3 )); echo ${z[@]} ${!z[@]}
)
# indirect elem
( a=(x y); n=a; echo ${!n}; echo ${!n[0]}; m=(a b); echo ${!m[0]}
)
# positional braced
( set -- a b c; echo "${@}|${*}|${#@}|${#*}"; echo "${@:-d}|${*:-d}"; set --; echo "[${@-x}][${*:-y}]"
)
# positional ops
( set -- foo.txt bar.txt; echo ${@%.txt}; echo "${@/o/0}"; echo ${@^^}; echo "${*#*.}"; echo ${@@Q}
)
