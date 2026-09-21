# read -a, bundled read options, and REPLY.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
# ifs ws+delim 1
( printf "%s\n" "a, b ,c" | { IFS=" ," read a b c; echo "[$a][$b][$c]"; }
)
# default ifs
( printf "%s\n" "  a   b   c d  " | { read x y z; echo "[$x][$y][$z]"; }
)
# backslash escapes
( printf "%s\n" "a\\ b c" | { read x y; echo "[$x][$y]"; }
)
# eof no newline
( printf "x y" | { read a b; echo "rc=$? [$a][$b]"; }
)
# a basic
( read -a a <<< "a b  c"; declare -p a
)
# a comma
( IFS=, read -a a <<< "a,b,,c"; declare -p a
)
# a trailing comma
( IFS=, read -a a <<< "a,b,c,"; declare -p a
)
# a leading comma
( IFS=, read -a a <<< ",a"; declare -p a
)
# a colons
( IFS=: read -a a <<< ":a:b:"; declare -p a
)
# a empty
( read -a a <<< ""; declare -p a
)
# a blanks
( read -a a <<< "   "; declare -p a
)
# a lead trail ws
( read -a a <<< "  lead and trail  "; declare -p a
)
# a backslash
( read -a a <<< "a\ b c"; declare -p a
)
# a raw
( read -r -a a <<< "a\ b c"; declare -p a
)
# a bundled
( read -ra a <<< "x  y"; declare -p a; read -rd , -a b <<< "p q,r"; declare -p b
)
# a eof
( printf "x y z" | { read -a a; echo rc=$?; declare -p a; }
)
# a replaces
( a=(old old old old); read -a a <<< "n1 n2"; declare -p a
)
# a extra name
( read -a a x <<< "1 2 3"; echo rc=$?; declare -p a; declare -p x 2>/dev/null; echo xrc=$?
)
# a mixed ifs
( IFS=" ," read -a a <<< "a, b ,c"; declare -p a; IFS=" :" read -a b <<< "a : b :  : c"; declare -p b
)
# a ifs empty
( IFS= read -a a <<< "a b c"; declare -p a; IFS= read -a a <<< ""; declare -p a; IFS= read -a a <<< "  x y  "; declare -p a
)
# a double delim
( IFS=: read -a a <<< "x::"; declare -p a; IFS=: read -a b <<< "::"; declare -p b; IFS=: read -a c <<< "a:  :b"; declare -p c
)
# a -d
( read -d , -a a <<< "p q,r s"; declare -p a
)
# a -n
( read -n 3 -a a <<< "abcdef"; declare -p a; read -n 5 -a b <<< "a b c d"; declare -p b
)
# a in function
( f() { local -a loc; read -ra loc <<< "1 2"; echo ${#loc[@]}; }; f
)
# a existing array
( declare -a pre=(1 2); read -a pre <<< "z"; declare -p pre
)
# a attrs
( declare -ia n; read -a n <<< "1+1 2*3"; declare -p n; declare -ua u; read -a u <<< "ab cd"; declare -p u
)
# a fd
( exec 3<<< "u1 u2"; read -u 3 -a a; declare -p a
)
# a errors
( read -a 1bad <<< x 2>/dev/null; echo rc=$?; declare -A A; read -a A <<< "x y" 2>/dev/null; echo rc=$?; readonly R=1; read -a R <<< x 2>/dev/null; echo rc=$?; read -a 2>/dev/null <<< x; echo rc=$?
)
# a index
( read -a a <<< "p q r"; echo "${a[1]} ${#a[@]} ${a[@]:1}"; for w in "${a[@]}"; do echo "<$w>"; done
)
# a loop idiom
( line="k=v;x=y"; IFS=";" read -ra parts <<< "$line"; for p in "${parts[@]}"; do IFS== read -r k v <<< "$p"; echo "$k -> $v"; done
)
# a read REPLY
( read <<< "  a b  "; echo "[$REPLY]"; read -r <<< "  a\b  "; echo "[$REPLY]"; read <<< "a\ b"; echo "[$REPLY]"
)
