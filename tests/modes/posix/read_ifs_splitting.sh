# `read` splits a line by IFS the way dash does: IFS whitespace is trimmed and merged, any other IFS
# character is a delimiter of its own, and the last name takes the rest (a single trailing delimiter is dropped).
# ifs ws+delim 1
( printf "%s\n" "a, b ,c" | { IFS=" ," read a b c; echo "[$a][$b][$c]"; }
)
# ifs delim ws last
( printf "%s\n" "1 , 2 , 3" | { IFS=", " read a b; echo "[$a][$b]"; }
)
# ifs empty field
( printf "%s\n" "x::z" | { IFS=: read a b c; echo "[$a][$b][$c]"; }
)
# ifs trailing delim last
( printf "%s\n" "x:y:" | { IFS=: read a b; echo "[$a][$b]"; }
)
# ifs trailing delim single
( printf "%s\n" "x:" | { IFS=: read a; echo "[$a]"; }
)
# ifs double trailing
( printf "%s\n" "x::" | { IFS=: read a; echo "[$a]"; }
)
# ifs leading delim
( printf "%s\n" ",y" | { IFS=, read a b; echo "[$a][$b]"; }
)
# ifs mixed lead
( printf "%s\n" " x : y  z" | { IFS=" :" read a b c; echo "[$a][$b][$c]"; }
)
# ifs nonws lead ws kept
( printf "%s\n" "  x :y" | { IFS=: read a b; echo "[$a][$b]"; }
)
# ifs rest keeps delims
( printf "%s\n" "x:y:z" | { IFS=: read a b; echo "[$a][$b]"; }
)
# ifs ws then delim end
( printf "%s\n" "x :" | { IFS=" :" read a; echo "[$a]"; }
)
# ifs ws delim ws end
( printf "%s\n" "x : " | { IFS=" :" read a; echo "[$a]"; }
)
# ifs comma end
( printf "%s\n" "a,b," | { IFS=", " read a b; echo "[$a][$b]"; }
)
# default ifs
( printf "%s\n" "  a   b   c d  " | { read x y z; echo "[$x][$y][$z]"; }
)
# more names than fields
( printf "%s\n" "a b" | { read x y z; echo "[$x][$y][$z]"; }
)
# empty line
( printf "\n" | { read x y; echo "[$x][$y]"; }
)
# backslash escapes
( printf "%s\n" "a\\ b c" | { read x y; echo "[$x][$y]"; }
)
# backslash -r
( printf "%s\n" "a\\ b c" | { read -r x y; echo "[$x][$y]"; }
)
# escaped delim
( printf "%s\n" "a\\:b:c" | { IFS=: read x y; echo "[$x][$y]"; }
)
# eof no newline
( printf "x y" | { read a b; echo "rc=$? [$a][$b]"; }
)
# ifs empty
( printf "%s\n" "  x  y " | { IFS= read a b; echo "[$a][$b]"; }
)
# tab ifs
( printf "a b\tc d\t\te\n" | { IFS="$(printf "\t")" read a b c; echo "[$a][$b][$c]"; }
)
# bundled -r
( printf "%s\n" "a\\b" | { read -r x; echo "[$x]"; }
)
# read invalid name rc
( read 1bad </dev/null 2>/dev/null; echo rc=$?
)
