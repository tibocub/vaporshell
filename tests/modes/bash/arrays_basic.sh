# Indexed arrays: literals, assignment forms, every expansion form, slices, per-element operators, scoping.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
# basic literal
( a=(x y z); echo ${a[0]} ${a[1]} ${a[2]} "[${a[3]}]"; echo $a ${a}
)
# all forms
( a=(x "y y" z); echo ${a[@]}; echo ${a[*]}; echo "${a[@]}"; echo "${a[*]}"; for e in "${a[@]}"; do echo "<$e>"; done; for e in ${a[@]}; do echo "<$e>"; done; for e in "${a[*]}"; do echo "<$e>"; done
)
# ifs join
( a=(x y z); IFS=:; echo "${a[*]}"; echo ${a[*]}; IFS=; echo "${a[*]}"
)
# counts
( a=(x "y y" z); echo ${#a[@]} ${#a[*]} ${#a[1]} ${#a} ${#a[9]}
)
# sparse
( a[5]=z; a[2]=y; echo ${#a[@]} ${!a[@]}; echo "${a[@]}"; a+=(w); echo ${!a[@]}; echo ${a[6]}
)
# indices
( a=(p q r); echo ${!a[@]}; echo "${!a[@]}"; for i in "${!a[@]}"; do echo $i=${a[$i]}; done; echo "${!a[*]}"
)
# explicit idx
( a=(x [3]=y z [1]=w); echo ${!a[@]}; echo ${a[@]}; b=([2]=a [5]=b c d); echo ${!b[@]}
)
# append
( a=(x); a+=(y z); echo ${a[@]}; a+=w; echo ${a[@]}; a[1]+=Q; echo ${a[@]}; a+=([9]=n m); echo ${!a[@]}
)
# scalar to array
( s=v; s[2]=x; echo ${s[@]} ${!s[@]}; t=hello; t+=(a b); echo ${t[@]} ${#t[@]}
)
# assign scalar on array
( a=(1 2 3); a=x; echo ${a[@]}; a+=y; echo ${a[@]}
)
# empty
( a=(); echo ${#a[@]} "[${a[@]}]" "[$a]"; a+=(x); echo ${a[@]}
)
# negative
( a=(a b c d); echo ${a[-1]} ${a[-2]} ${a[-4]}; a[-1]=Z; echo ${a[@]}; a[-4]=A; echo ${a[@]}
)
# neg sparse
( a=(a b c); unset a; a[3]=x; a[6]=y; echo ${a[-1]} ${a[-3]}
)
# arith index
( a=(a b c d); i=1; echo ${a[i]} ${a[$i]} ${a[i+1]} ${a[i*2]} ${a[(i+2)]}; a[i+1]=Z; echo ${a[@]}
)
# spaces in elems
( a=("b c" "d  e"); set -- "${a[@]}"; echo $#; set -- ${a[@]}; echo $#; set -- "${a[*]}"; echo $#
)
# globs
( : > x.txt; : > y.txt; a=(*.txt); echo ${#a[@]} "${a[@]}"; b=("*.txt"); echo "${b[0]}"
)
# multi-line
( a=(
  one   # first
  "two words"
  three
)
echo ${#a[@]} "${a[1]}"
)
# word splitting
( x="p q"; a=($x r); echo ${#a[@]}; a=("$x" r); echo ${#a[@]}
)
# positional in array
( set -- a "b c"; a=("$@"); echo ${#a[@]} "${a[1]}"; a=($@); echo ${#a[@]}
)
# cmdsub elems
( a=($(echo 1 2 3) "$(echo x y)"); echo ${#a[@]}
)
# quoted parts
( a=(a\ b "c d" e"f g"h); echo ${#a[@]}; echo "${a[2]}"
)
# brace in literal
( a=(x{1,2} {a..c}); echo ${a[@]} ${#a[@]}
)
# slice
( a=(a b c d e); echo ${a[@]:1} ${a[@]:1:2} ${a[@]:3:10} "[${a[@]:5}]" "[${a[@]:1:0}]"
)
# slice neg
( a=(a b c d e); echo ${a[@]: -2} ${a[@]: -3:2} ${a[@]:0:-1}
)
# slice sparse
( a=(a b c d e); unset a[1]; echo ${a[@]:1:2}; echo ${a[@]:2}; echo "${a[@]: -2}"
)
# slice star
( a=(a b c); IFS=,; echo "${a[*]:1}"; echo "${a[@]:1}"
)
# trim
( a=(foo.txt bar.txt baz.c); echo ${a[@]%.txt}; echo ${a[@]%%.*}; echo ${a[@]#*.}; echo ${a[@]##*a}
)
# replace
( a=(hello hallo world); echo ${a[@]/l/L}; echo ${a[@]//l/L}; echo ${a[@]/#h/H}; echo ${a[@]/%o/0}; echo ${a[@]/l}
)
# replace amp
( a=(ab cb); echo ${a[@]/b/[&]}
)
# case
( a=(hello World); echo ${a[@]^} ${a[@]^^} ${a[@],} ${a[@],,} ${a[@]^^[lo]}
)
# transform
( a=("a b" c); echo ${a[@]@Q}; echo ${a[@]@U}; echo ${a[@]@u}; echo ${a[@]@L}
)
# quoted per-element
( a=("a b" "c d"); set -- "${a[@]/ /_}"; echo $#; echo "$@"; set -- "${a[*]/ /_}"; echo $#; set -- ${a[@]/ /_}; echo $#
)
# per-element star ifs
( a=(x y z); IFS=-; echo "${a[*]/x/X}"; echo "${a[@]/x/X}"
)
# defaults list
( a=(x y); echo ${a[@]:-none}; b=(); echo ${b[@]:-none}; echo ${u[@]:-none}; echo ${a[@]:+alt}; echo "[${b[@]:+alt}]"; echo ${a[@]-n} ${b[@]-n} ${a[@]+p}
)
# element defaults
( a=(x "" z); echo ${a[0]:-d} ${a[1]:-d} ${a[1]-d} ${a[7]:-d} ${a[0]:+alt} ${a[7]:+alt}
)
# element assign default
( a=(x); echo ${a[3]:=v}; echo ${!a[@]} ${a[3]}
)
# element ops
( a=(hello.txt "wor ld"); echo ${a[0]%.txt} ${a[0]:1:3} ${a[0]/l/L} ${a[0]^^} ${a[1]//o/0} ${#a[1]} ${a[0]@Q}
)
# element trim star
( a=(foo.bar); echo ${a[0]##*.} ${a[0]%%.*}
)
# positional braced
( set -- a b c; echo "${@}|${*}|${#@}|${#*}"; echo "${@:-d}|${*:-d}"; set --; echo "[${@-x}][${*:-y}]"
)
# positional ops
( set -- foo.txt bar.txt; echo ${@%.txt}; echo "${@/o/0}"; echo ${@^^}; echo "${*#*.}"; echo ${@@Q}
)
# positional ifs
( set -- a b c; IFS=:; echo "${*}"; echo "$*"; echo ${*}
)
# positional slice
( set -- a b c d; echo "${@:2}" "${*:2:2}" "${@: -1}"
)
# prefix assign array
( a=(1 2 3); a=x true; echo ${a[@]}; a[1]=y true; echo ${a[@]}
)
# subshell isolation
( a=(1 2); (a[0]=9; a+=(3); echo ${a[@]}); echo ${a[@]}; echo $(a+=(4); echo ${a[@]}) ${a[@]}
)
# function arrays
( f() { a+=("$@"); }; f 1 2; f 3; echo ${a[@]} ${#a[@]}; g() { echo "${!a[@]}"; }; g
)
# unset var
( a=(1 2); unset a; echo "[${a[@]}]" ${#a[@]}; a[2]=x; echo ${a[@]} ${!a[@]}
)
# array in arith ctx
( a=(1 2 3); echo $(( ${a[1]} + ${a[2]} ))
)
# indirect elem
( a=(x y); n=a; echo ${!n}; echo ${!n[0]}
)
# scalar plus
( x=a; x+=b; x+=c; echo $x; y+=z; echo $y; PATH+=:/zz; case $PATH in *:/zz) echo ok;; esac
)
# elem of scalar
( s=hello; echo ${s[0]} ${s[@]} ${#s[@]} "[${s[1]}]" ${!s[@]}
)
# unset elem read
( echo "[${nope[0]}][${nope[@]}][${#nope[@]}]"
)
# elem with brackets
( a=(x y); i=0; echo ${a[i]} ${a[i+1]} ${a[$((i+1))]}
)
# nested subscript
( a=(0 1 2); b=(2 0); echo ${a[${b[0]}]} ${a[b[1]]}
)
# while read into array via +=
( a=(); for w in x y z; do a+=("$w"); done; echo ${#a[@]} ${a[2]}
)
# copy array
( a=(x "y z"); b=("${a[@]}"); b[0]=Q; echo "${a[@]}|${b[@]}"
)
# array literal with = in elem
( a=(k=v "x=y"); echo ${a[0]} ${a[1]}
)
# double index
( a=(a b); echo ${a[0]}${a[1]} $a[1]
)
