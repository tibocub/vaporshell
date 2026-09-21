# declare/typeset/local/export/readonly, attributes, associative arrays.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
_sort() { local i j k; for (( i = 1; i < ${#_s[@]}; i++ )); do k=${_s[i]}; j=$i; while (( j > 0 )) && [[ ${_s[j-1]} > $k ]]; do _s[j]=${_s[j-1]}; (( j-- )); done; _s[j]=$k; done; }
# declare -p scalars
( s=v; declare -p s; export x=1; declare -p x; readonly r=2; declare -p r; declare -i n=3+4; declare -p n; declare u; declare -p u
)
# declare -p arrays
( declare -a a=(1 "b c"); declare -p a; declare -a e; declare -p e; declare -A ea; declare -p ea; f=(); declare -p f
)
# declare -p assoc
( declare -A m=([k]=v); declare -p m; declare -A q=([a]=1); q[b]=2; echo ${#q[@]} ${q[a]} ${q[b]}
)
# attr order
( declare -aix z=(1 2); declare -p z; declare -lr lo=ABC; declare -p lo; declare -u up=abc; declare -p up; declare -ir ir=5; declare -p ir
)
# declare -p missing
( declare -p nope; echo rc=$?
)
# declare bad name
( declare 1x=2; echo rc=$?; declare "a b"=1; echo rc=$?
)
# integer
( declare -i i=2; i+=3; echo $i; i=4*5; echo $i; i=abc; echo $i; declare -i j; j=7; echo $j; declare -i k=1; k+=1+1; echo $k
)
# integer array
( declare -ia a=(1+1 2*3); echo ${a[@]}; a[2]=5+5; echo ${a[@]}; a+=(1+1); echo ${a[@]}
)
# case attrs
( declare -u U=abc; U+=def; echo $U; declare -l L=ABC; L=XyZ; echo $L; declare -u X=a; declare -l X; X=ABC; echo $X
)
# scope function
( f() { declare v1=in; local v2=in; declare -g v3=in; }; f; echo "[$v1][$v2][$v3]"
)
# scope outer restored
( v=out; f() { declare v=in; echo $v; }; f; echo $v; g() { local -i v=5; v+=1; echo $v; }; g; echo $v
)
# export/readonly arrays
( export ea=(1 2); declare -p ea; readonly ra=(1 2); declare -p ra
)
# multiple names
( declare -a p=(1 2) q=(3); declare -p p q
)
# splitting
( w="1 2"; declare sp=$w; echo "[$sp]"; declare -a sq=($w); echo ${#sq[@]}; export ex=$w; echo "[$ex]"; local_t() { local lv=$w; echo "[$lv]"; }; local_t
)
# attr removal
( declare -x ex=1; declare +x ex; declare -p ex; declare -i ii=5; declare +i ii; declare -p ii; declare -u uu=a; declare +u uu; uu=b; echo $uu
)
# existing arrays
( ee=(1 2); declare -a ee; declare -p ee; declare ee=x; declare -p ee; declare -x ee; declare -p ee
)
# scalar to array
( s=v; declare -a s; declare -p s; t=w; declare -A t; declare -p t
)
# cannot convert
( declare -a ia=(1 2); declare -A ia; echo rc=$?; declare -A aa=([k]=v); declare -a aa; echo rc=$?
)
# assoc basics
( declare -A m=([a]=1 [b]=2); echo ${m[a]} ${m[b]} ${#m[@]}; m[c]=3; m[a]+=x; echo ${m[a]} ${m[c]}; echo ${m[nokey]:-none}; k=b; echo ${m[$k]} ${m["b"]}
)
# assoc keys
( declare -A m=([a]=1 [b]=2 [c]=3); _s=("${!m[@]}"); _sort; for k in "${_s[@]}"; do echo "$k=${m[$k]}"; done; _s=("${m[@]}"); _sort; echo "${_s[*]}"
)
# assoc spaces
( declare -A o=([a b]=1 ["c d"]=2 [e]=); echo ${#o[@]}; echo "${o[a b]}|${o[c d]}|[${o[e]}]"
)
# assoc pairs
( declare -A m=(a 1 b 2); echo ${m[a]} ${m[b]} ${#m[@]}
)
# assoc distinct keys
( declare -A q=([1]=a [01]=b); echo ${q[1]} ${q[01]} ${#q[@]}
)
# assoc append replace
( declare -A r=([k]=v); r+=([j]=w); echo ${#r[@]}; r=([z]=1); echo ${!r[@]}
)
# assoc elem zero
( declare -A s=([k]=v); echo "${s[0]:-none}" "[$s]"; s[0]=zero; echo $s ${s[0]}
)
# scalar to assoc
( x=scalar; declare -A x; declare -p x
)
# assoc arith
( declare -A t=([a]=1); (( t[a] += 5 )); echo ${t[a]}; k=a; (( t[$k]++ )); echo ${t[a]}; echo $(( t[a] * 2 )); declare -A u=([foo]=1); foo=zz; echo $(( u[foo] )); (( u[bar] = 7 )); echo ${u[bar]}
)
# assoc unset -v
( declare -A w=([a]=1 [b]=2); unset "w[a]"; echo ${!w[@]}; [[ -v w[b] ]] && echo v-b; [[ -v w[zz] ]] || echo no-zz; unset "w[@]"; echo ${#w[@]}; unset w; echo "[${w[@]}]"
)
# assoc default assign
( declare -A a; echo ${a[x]:=v}; echo ${a[x]} ${#a[@]}
)
# assoc in function
( f() { local -A m=([k]=v); echo ${m[k]}; }; f; [[ -v m ]] || echo m-not-global; g() { declare -gA gm=([x]=1); }; g; echo ${gm[x]}
)
# assoc copy
( declare -A a=([x]=1); declare -A b; for k in "${!a[@]}"; do b[$k]=${a[$k]}; done; echo ${b[x]}
)
# assoc quoted
( declare -A a=(["x y"]="1 2"); echo "${a[x y]}"; declare -p a
)
# declare -p filter
( a=(1); declare -A b=([k]=v); c=s; declare -p | while IFS= read -r l; do case $l in "declare -a a="*|"declare -A b="*) echo "$l";; esac; done | while IFS= read -r l; do echo "${l%%=*}"; done
)
# export -p
( export ZED=1; export -p | while IFS= read -r l; do case $l in "declare -x ZED="*) echo "$l";; esac; done; export -n ZED; export -p | while IFS= read -r l; do case $l in "declare -x ZED"*) echo "$l";; esac; done
)
# readonly -p
( readonly RO=1; readonly -p | while IFS= read -r l; do case $l in "declare -r RO="*) echo "$l";; esac; done; RO2=a; readonly RO2; declare -p RO2
)
# typeset
( typeset -i t=1+1; declare -p t; typeset -a ta=(x); declare -p ta
)
# literal with vars
( x=1; y="a b"; declare -a a=($x "$y" z); echo ${#a[@]} "${a[1]}"; declare -A m=([$x]=v [k]="$y"); echo ${m[1]} "${m[k]}"
)
# literal multiline
( declare -a a=(
  one
  # comment
  "two words"
); echo ${#a[@]} "${a[1]}"
)
# local array
( f() { local a=(p q); a+=(r); echo ${a[@]}; }; a=(1 2); f; echo ${a[@]}; g() { local -a b; b[2]=x; echo ${!b[@]}; }; g
)
# local -i
( f() { local -i n=2; n+=3; echo $n; local -u s=ab; echo $s; }; f
)
# readonly attr
( declare -r rr=1; declare -p rr; declare -x -r xr=2; declare -p xr; readonly -a ra2=(1); declare -p ra2
)
# assign after decl
( declare -i c; c=1+2; echo $c; c+=4; echo $c; declare -a d; d[3]=x; d+=(y); echo ${!d[@]}
)
# declare value with dq
( declare v="a\"b\$c"; declare -p v; declare w=$'t\tab'; declare -p w
)
# declare no args count
( aa=1; declare | { n=0; while IFS= read -r l; do case $l in aa=*) n=$((n+1));; esac; done; echo $n; }
)
