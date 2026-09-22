# Namerefs: declare -n / typeset -n / local -n, +n, unset -n. Every access to a reference lands on the variable it
# names -- reads, assignments, arrays, elements ('a[1]'), read, printf -v, mapfile, arithmetic -- through a chain
# of references; `unset r` unsets the target, `unset -n r` the reference; ${!r} is the target's name; a for loop
# rebinds the reference. Expected output is bash's.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
# basic
( declare -n r=x; x=1; echo "$r"; r=2; echo "$x"; declare -p r
)
# typeset
( typeset -n r=x; x=1; echo "$r"
)
# unset target
( declare -n r=x; x=1; unset r; echo "[${x-gone}] [${r-nr}]"; declare -p r
)
# unset -n
( x=5; declare -n r=x; unset -n r; echo "x=$x r=[${r-nr}]"; declare -p r 2>/dev/null; echo "rc=$?"
)
# unset -v
( x=5; declare -n r=x; unset -v r; echo "[${x-gone}]"
)
# indirect name
( declare -n r=x; x=5; echo "${!r}"
)
# array read
( declare -n r=a; a=(1 2 3); echo "${r[1]} ${#r[@]} ${r[@]} ${!r[@]}"
)
# array write
( declare -n r=a; a=(1 2 3); r[3]=4; r+=(5); r[0]=9; echo "${a[@]}"
)
# array unset elem
( declare -n r=a; a=(1 2 3); unset "r[1]"; echo "${a[@]} ${!a[@]}"
)
# assoc through ref
( declare -A m=([k]=v); declare -n r=m; echo "${r[k]}"; r[j]=w; echo "${m[j]} ${#m[@]}"
)
# assoc created via ref
( declare -n r=mm; declare -A mm; r[a]=1; r[b]=2; echo "${#mm[@]} ${mm[a]}"
)
# element target
( a=(p q r); declare -n e="a[1]"; echo "$e"; e=Q; echo "${a[@]}"
)
# element target assoc
( declare -A m=([k]=v); declare -n e="m[k]"; echo "$e"; e=V; echo "${m[k]}"
)
# chain
( declare -n a1=b1; declare -n b1=c1; c1=9; echo "$a1"; a1=10; echo "$c1"
)
# chain rebind
( declare -n a1=b1; declare -n b1=c1; c1=1; d1=2; declare -n b1=d1; echo "$a1"
)
# local -n
( f() { local -n out=$1; out=result; }; unset res; f res; echo "$res"
)
# local -n array
( g() { local -n arr=$1; arr+=(x y); }; lst=(a); g lst; echo "${lst[@]}"
)
# local -n restored
( f() { local -n r=$1; r=1; }; f v; declare -p r 2>/dev/null; echo "rc=$?"; echo "$v"
)
# local -n nested
( inner() { local -n i=$1; i+=1; }; outer() { local -n o=$1; inner o; inner o; }; n=0; outer n; echo "$n"
)
# local shadows
( x=outer; f() { local x=inner; local -n r=x; r=changed; echo "in: $x"; }; f; echo "$x"
)
# declare in function
( f() { declare -n out=$1; out=result; }; f res; echo "$res"
)
# declare -g in function
( f() { declare -gn out=$1; out=result; }; f res 2>/dev/null; echo "$res [${out-unset}]"
)
# unset target then create
( declare -n u=newvar; echo "[${u-unset}]"; u=created; echo "$newvar"
)
# no target then assign
( declare -n nt; nt=tgt; tgt=7; echo "$nt"; declare -p nt
)
# existing value is target
( ev=target; tv=T; declare -n ev; echo "[$ev]"; ev=NEW; echo "tv=$tv target=$target"
)
# +n
( declare -n p=x; x=val; declare +n p; echo "[$p]"; declare -p p
)
# arithmetic
( declare -n c=cnt; cnt=1; (( c++ )); echo "$cnt"; echo "$(( c + 10 ))"; (( c += 5 )); echo "$cnt"
)
# read
( declare -n rr=tgt; read rr <<< "hello world"; echo "$tgt"
)
# read -a
( declare -n rr=tgt; read -a rr <<< "a b c"; echo "${tgt[1]} ${#tgt[@]}"
)
# printf -v
( declare -n rr=tgt; printf -v rr "%s-%s" a b; echo "$tgt"
)
# mapfile
( declare -n rr=tgt; printf "l1\nl2\n" | { mapfile -t rr; echo "${tgt[1]}"; }; printf "l1\nl2\n" > f.txt; mapfile -t rr < f.txt; echo "${tgt[@]}"
)
# -v test
( declare -n t=setv; [[ -v t ]] && echo set || echo unset; setv=1; [[ -v t ]] && echo set || echo unset
)
# defaults
( declare -n d=dd; echo "${d:-def}"; echo "${d:=asg} $dd"; echo "${d:+alt}"; echo "${#d}"
)
# string ops
( declare -n s=str; str=hello; echo "${s^^} ${s:1:3} ${s/l/L} ${s#h} ${s%o}"
)
# invalid target
( declare -n bad=1x 2>/dev/null; echo "rc=$?"; declare -n bad2="a b" 2>/dev/null; echo "rc=$?"; declare -n bad3="a[" 2>/dev/null; echo "rc=$?"
)
# self reference
( declare -n self=self 2>/dev/null; echo "rc=$?"; declare -p self 2>/dev/null; echo "rc=$?"
)
# circular
( declare -n c1x=c2x; declare -n c2x=c1x; echo "[$c1x]" 2>/dev/null
)
# array cannot be ref
( a=(1 2); declare -n a 2>/dev/null; echo "rc=$?"; declare -p a
)
# for rebind
( declare -n it=one; one=1; two=2; for it in one two; do echo -n "$it "; done; echo; echo "one=$one two=$two"
)
# for over names
( a=x; b=y; declare -n r; for r in a b; do echo "$r"; done
)
# export target
( declare -n ex=exv; exv=1; export ex; declare -p exv
)
# readonly target
( declare -n rt=rov; rov=1; readonly rt; declare -p rov
)
# integer attr target
( declare -n n=nv; declare -i nv; n=3+4; echo "$nv"
)
# declare -p forms
( x=1; declare -n r=x; declare -p r x
)
# declare -n listing
( x=1; y=2; declare -n r1=x; declare -n r2=y; declare -n | { n=0; while IFS= read -r l; do case $l in "declare -n r1="*|"declare -n r2="*) n=$((n+1));; esac; done; echo $n; }
)
# declare -pn
( x=1; declare -n r=x; declare -pn | { n=0; while IFS= read -r l; do case $l in "declare -n r="*) n=$((n+1));; esac; done; echo $n; }
)
# attribute order
( declare -nr rr=xx 2>&1; declare -p rr
)
# set lists
( x=1; declare -n r=x; set | { while IFS= read -r l; do case $l in r=*) echo "$l";; esac; done; }
)
# function sees ref
( x=1; declare -n r=x; f() { echo "$r"; r=2; }; f; echo "$x"
)
# subshell
( x=1; declare -n r=x; ( r=5; echo "$x" ); echo "$x"
)
# cmdsub
( x=1; declare -n r=x; y=$(r=9; echo "$r"); echo "$y $x"
)
# eval
( x=1; declare -n r=x; eval "r=7"; echo "$x"; eval 'echo "$r"'
)
# swap idiom
( swap() { local -n p=$1 q=$2; local t=$p; p=$q; q=$t; }; a=1; b=2; swap a b; echo "$a $b"
)
# array param idiom
( sum() { local -n arr=$1; local t=0 i; for i in "${arr[@]}"; do (( t += i )); done; echo $t; }; nums=(1 2 3 4); sum nums
)
# return via ref
( get() { local -n __r=$1; __r="value from f"; }; get answer; echo "$answer"
)
# assoc iterate via ref
( declare -A m=([a]=1 [b]=2); f() { local -n h=$1; local k t=""; for k in "${!h[@]}"; do t+="$k=${h[$k]};"; done; [[ $t == "a=1;b=2;" || $t == "b=2;a=1;" ]] && echo both; }; f m
)
