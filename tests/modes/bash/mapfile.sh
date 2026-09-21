# mapfile / readarray: options, callbacks, the array it fills, and what it reads from its input.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
# default keeps newline
( printf "a\nb b\n\nc\n" > f.txt; mapfile a < f.txt; declare -p a
)
# -t
( printf "a\nb b\n\nc\n" > f.txt; mapfile -t b < f.txt; declare -p b
)
# -n
( printf "a\nb b\n\nc\n" > f.txt; mapfile -t -n 2 c < f.txt; declare -p c
)
# -s
( printf "a\nb b\n\nc\n" > f.txt; mapfile -t -s 1 d < f.txt; declare -p d
)
# -s -n
( printf "a\nb b\n\nc\n" > f.txt; mapfile -t -s 1 -n 2 e < f.txt; declare -p e
)
# -O keeps array
( printf "a\nb b\n\nc\n" > f.txt; x=(p q r); mapfile -t -O 1 x < f.txt; declare -p x
)
# -O 0 does not clear
( printf "a\nb b\n\nc\n" > f.txt; x=(p q r s t u); mapfile -t -O 0 x < f.txt; declare -p x
)
# -O new array
( printf "a\nb b\n\nc\n" > f.txt; mapfile -t -O 3 y < f.txt; declare -p y
)
# empty input
( mapfile -t f < /dev/null; declare -p f; x=(1 2); mapfile -t x < /dev/null; declare -p x
)
# MAPFILE default
( printf "a\nb b\n\nc\n" > f.txt; mapfile -t < f.txt; declare -p MAPFILE
)
# readarray
( printf "a\nb b\n\nc\n" > f.txt; readarray -t g < f.txt; echo ${#g[@]}; readarray h < f.txt; echo ${#h[@]}
)
# -d comma
( printf "a,b,c" | { mapfile -d , -t h; declare -p h; }
)
# -d comma keep
( printf "a,b,c," | { mapfile -d , h; declare -p h; }
)
# -d nul
( printf "a\0b\0" | { mapfile -d "" -t i; declare -p i; }
)
# no trailing newline
( printf "no-trailing-newline" | { mapfile -t j; declare -p j; }
)
# no trailing newline keep
( printf "x\ny" | { mapfile k; declare -p k; }
)
# callback -c
( printf "1\n2\n3\n4\n" | { mapfile -t -c 2 -C "echo cb" k; declare -p k; }
)
# callback function
( printf "l1\nl2\nl3\n" | { cb() { echo "cb idx=$1 line=[$2]"; }; mapfile -t -c 1 -C cb a; declare -p a; }
)
# callback keeps newline
( printf "l1\nl2\n" | { cb() { printf "cb %s %q\n" "$1" "$2"; }; mapfile -c 1 -C cb a; }
)
# callback quoting
( printf "it's\n\$x\n" | { cb() { echo "[$2]"; }; mapfile -t -c 1 -C cb a; }
)
# callback default quantum
( for ((i=1;i<=6000;i++)); do echo $i; done | { cb() { echo "cb $1"; }; mapfile -t -C cb a; echo ${#a[@]}; }
)
# callback sets var
( printf "a\nb\nc\n" | { n=0; cb() { n=$((n+1)); }; mapfile -t -c 1 -C cb a; echo n=$n; }
)
# callback exit
( printf "a\nb\nc\n" | { cb() { exit 7; }; mapfile -t -c 1 -C cb a; echo not-reached; }; echo rc=$?
)
# -u fd
( printf "a\nb b\n\nc\n" > f.txt; exec 3<f.txt; mapfile -t -u 3 m; declare -p m; exec 3<&-
)
# -n 0 all
( printf "a\nb b\n\nc\n" > f.txt; mapfile -t -n 0 n < f.txt; declare -p n
)
# bundled opts
( printf "a\nb b\n\nc\n" > f.txt; mapfile -tn 2 a < f.txt; declare -p a; mapfile -tO1 -s1 b < f.txt; declare -p b
)
# consumption after -n
( printf "a\nb b\n\nc\n" > f.txt; { mapfile -t -n 2 a; echo "${a[@]}"; while IFS= read -r l; do echo "rest:$l"; done; } < f.txt
)
# consumption then read
( printf "a\nb b\n\nc\n" > f.txt; { mapfile -t -n 1 a; read -r x; read -r y; echo "[${a[0]}][$x][$y]"; } < f.txt
)
# in function local
( printf "a\nb b\n\nc\n" > f.txt; f() { local -a arr; mapfile -t arr < f.txt; echo ${#arr[@]}; }; f; declare -p arr 2>/dev/null; echo rc=$?
)
# existing scalar
( printf "a\nb b\n\nc\n" > f.txt; x=scalar; mapfile -t x < f.txt; declare -p x
)
# attrs integer
( declare -ia n; printf "1+1\n2*3\n" | { mapfile -t n; declare -p n; }; printf "1+1\n2*3\n" > g.txt; mapfile -t n < g.txt; declare -p n
)
# attrs upper
( declare -ua u; printf "ab\ncd\n" > g.txt; mapfile -t u < g.txt; declare -p u
)
# errors rc
( mapfile -t 1bad </dev/null 2>/dev/null; echo rc=$?; mapfile -t -n x a </dev/null 2>/dev/null; echo rc=$?; mapfile -z a </dev/null 2>/dev/null; echo rc=$?; mapfile -u 9 a 2>/dev/null; echo rc=$?; declare -A A; mapfile -t A </dev/null 2>/dev/null; echo rc=$?; readonly R=(1); mapfile -t R </dev/null 2>/dev/null; echo rc=$?; mapfile -O x a </dev/null 2>/dev/null; echo rc=$?; mapfile -s x a </dev/null 2>/dev/null; echo rc=$?; mapfile -c 0 a </dev/null 2>/dev/null; echo rc=$?; mapfile -u x a </dev/null 2>/dev/null; echo rc=$?; mapfile -n 2>/dev/null; echo rc=$?
)
# readonly untouched
( readonly R=(1 2); mapfile -t R < /dev/null 2>/dev/null; declare -p R
)
# assoc untouched
( declare -A A=([k]=v); mapfile -t A < /dev/null 2>/dev/null; declare -p A
)
# many lines
( for ((i=1;i<=2000;i++)); do echo $i; done | { mapfile -t a; echo ${#a[@]} ${a[0]} ${a[1999]}; }
)
# long line
( s=$(printf "%*s" 20000 ""); printf "%s\n" "${s// /x}" > long.txt; mapfile -t a < long.txt; echo ${#a[0]}
)
# crlf
( printf "a\r\nb\r\n" | { mapfile -t a; declare -p a; }
)
# empty lines
( printf "\n\n\n" | { mapfile -t a; declare -p a; mapfile a; declare -p a; }
)
# only newline
( printf "\n" | { mapfile -t a; declare -p a; }
)
# usage in loop
( printf "a\nb b\n\nc\n" > f.txt; mapfile -t lines < f.txt; for l in "${lines[@]}"; do echo "<$l>"; done; echo "last=${lines[-1]} count=${#lines[@]}"
)
# process pipeline
( for ((i=1;i<=5;i++)); do echo $i; done | { mapfile -t nums; echo "${nums[*]}"; s=0; for n in "${nums[@]}"; do (( s += n )); done; echo $s; }
)
# -s more than lines
( printf "a\nb b\n\nc\n" > f.txt; mapfile -t -s 10 a < f.txt; declare -p a
)
# -n more than lines
( printf "a\nb b\n\nc\n" > f.txt; mapfile -t -n 10 a < f.txt; echo ${#a[@]}
)
# array name via variable
( printf "a\nb b\n\nc\n" > f.txt; name=arr; mapfile -t "$name" < f.txt; echo ${#arr[@]}
)
# mapfile then unset
( printf "a\nb b\n\nc\n" > f.txt; mapfile -t a < f.txt; unset "a[1]"; echo "${!a[@]}"; mapfile -t -O 1 a < f.txt; echo "${!a[@]}"
)
