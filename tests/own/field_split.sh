x="a b  c"
for i in $x; do echo "[$i]"; done
for i in "$x"; do echo "[$i]"; done
IFS=:
y="a:b::c:"
for i in $y; do echo "[$i]"; done
IFS=" :"
z="a : b"
for i in $z; do echo "[$i]"; done
unset IFS
set -- "a b" c "d e f"
for i in "$@"; do echo "[$i]"; done
for i in $@; do echo "[$i]"; done
for i in "$*"; do echo "[$i]"; done
IFS=,; echo "$*"; echo $*; unset IFS
set --; echo "[$@]" "[$*]" $#
for i in "$@"; do echo never; done
e=""; for i in $e; do echo never; done; for i in "$e"; do echo "one empty"; done
echo a"$e"b '' x
mkdir -p gdir && cd gdir && : > a.txt && : > b.txt && : > .hid && mkdir sub && : > sub/c.txt
echo *.txt; echo *; echo .h*; echo */*.txt; echo "*.txt" '*.txt' \*.txt; echo [ab].txt; echo ?.txt
echo nomatch*.zzz
set -f; echo *.txt; set +f
q='*.txt'; echo $q "$q"
cd ..; rm -rf gdir
