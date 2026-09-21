# requires: regex
# [[ =~ ]] and BASH_REMATCH: groups, quoting, nesting, empty and unmatched groups.
# re groups
( [[ "foo bar" =~ ^(foo) (bar)$ ]] && echo "${BASH_REMATCH[1]}|${BASH_REMATCH[2]}"
)
# re space in group
( [[ "a b" =~ (a b) ]] && echo "m:${BASH_REMATCH[1]}"
)
# re alternation
( [[ cat =~ ^(cat|dog)$ ]] && echo "${BASH_REMATCH[1]}"; [[ fish =~ ^(cat|dog)$ ]] || echo none
)
# re bare pipe
( [[ b =~ a|b ]] && echo yes
)
# re class
( [[ "a  b" =~ [[:space:]]+ ]] && echo "sp:${#BASH_REMATCH[0]}"; [[ x1 =~ [[:alpha:]][[:digit:]] ]] && echo ok
)
# re var
( re="^([a-z]+)-([0-9]+)$"; [[ abc-12 =~ $re ]] && echo "${BASH_REMATCH[1]} ${BASH_REMATCH[2]}"
)
# re quoted literal
( p="a.c"; [[ abc =~ "$p" ]] || echo lit-no; [[ a.c =~ "$p" ]] && echo lit-yes; [[ abc =~ $p ]] && echo re-yes
)
# re mixed quoting
( [[ "a+b" =~ ^a"+"b$ ]] && echo yes; [[ "aab" =~ ^a"+"b$ ]] || echo no
)
# re escapes
( [[ "a.b" =~ ^a\.b$ ]] && echo yes; [[ "a b" =~ ^a\ b$ ]] && echo sp
)
# re anchors optional
( [[ hello =~ l+ ]] && echo "${BASH_REMATCH[0]}"; [[ hello =~ (x)?hello ]] && echo "[${BASH_REMATCH[1]}]"
)
# re nested groups
( [[ "2024-05-17" =~ ^(([0-9]+)-([0-9]+))-([0-9]+)$ ]] && echo "${BASH_REMATCH[@]}" "${#BASH_REMATCH[@]}"
)
# re && ||
( [[ a =~ a && b =~ b ]] && echo both; [[ a =~ x || b =~ b ]] && echo either; [[ ! a =~ x ]] && echo neg
)
# re in if
( for s in "v1.2" "x" "v10.20"; do if [[ $s =~ ^v([0-9]+)\.([0-9]+)$ ]]; then echo "$s -> ${BASH_REMATCH[1]},${BASH_REMATCH[2]}"; else echo "$s no"; fi; done
)
# re braces
( [[ aaa =~ ^a{3}$ ]] && echo three; [[ aa =~ ^a{3}$ ]] || echo not3
)
# re cmdsub
( [[ ab =~ ^$(echo a)b$ ]] && echo cs
)
# re empty match
( [[ abc =~ x* ]] && echo "[${BASH_REMATCH[0]}] ${#BASH_REMATCH[0]}"
)
# re rematch cleared
( [[ ab =~ (a)(b) ]]; echo ${#BASH_REMATCH[@]}; [[ ab =~ zz ]]; echo ${#BASH_REMATCH[@]}
)
# re in function
( f() { [[ $1 =~ ^(.)(.)$ ]] && echo "${BASH_REMATCH[2]}${BASH_REMATCH[1]}"; }; f xy
)
# re nocasematch
( shopt -s nocasematch; [[ ABC =~ abc ]] && echo ci
)
# re bad regex
( [[ a =~ "(" ]] 2>/dev/null; echo rc=$?; [[ a =~ ( ]] 2>/dev/null; echo rc=$?
)
# rematch
( [[ a =~ (b) ]]; echo "11: ${PIPESTATUS[@]} [${BASH_REMATCH[@]}]"
[[ abc123 =~ ([a-z]+)([0-9]+) ]]; echo "12: ${#BASH_REMATCH[@]} ${BASH_REMATCH[0]} ${BASH_REMATCH[1]} ${BASH_REMATCH[2]}"
[[ zzz =~ (q) ]]; echo "13: [${BASH_REMATCH[@]}] ${#BASH_REMATCH[@]}"
[[ ab =~ (x)?(b) ]]; echo "14: ${#BASH_REMATCH[@]} [${BASH_REMATCH[1]}] [${BASH_REMATCH[2]}]"

)
# rematch use
( if [[ "key=value" =~ ^([^=]+)=(.*)$ ]]; then echo "${BASH_REMATCH[1]} -> ${BASH_REMATCH[2]}"; fi
re="^[0-9]+$"; [[ 42 =~ $re ]] && echo "num ${BASH_REMATCH[0]}"
[[ x =~ y ]] || echo "no [${BASH_REMATCH[0]}]"

)
