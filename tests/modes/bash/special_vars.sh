# PIPESTATUS, FUNCNAME, BASH_SOURCE, BASH_LINENO, BASH_VERSINFO.
# pipestatus first
echo "first: ${PIPESTATUS[@]}"

# pipestatus basics
false; echo "1: ${PIPESTATUS[@]}"
true | false | true; echo "2: ${PIPESTATUS[*]} ${#PIPESTATUS[@]}"
echo hi >/dev/null; echo "3: ${PIPESTATUS[@]}"
( exit 3 ); echo "4: ${PIPESTATUS[@]}"
x=1; echo "5: ${PIPESTATUS[@]}"

# pipestatus negation
! false; echo "6: ${PIPESTATUS[@]}"
! true | false; echo "6b: ${PIPESTATUS[@]} $?"

# pipestatus compound
if false | true; then :; fi; echo "7: ${PIPESTATUS[@]}"
while false; do :; done; echo "7b: ${PIPESTATUS[@]}"
for i in 1 2; do false; done; echo "7c: ${PIPESTATUS[@]}"

# pipestatus function
f() { return 4; }; f; echo "8: ${PIPESTATUS[@]}"
g() { false | true; }; g; echo "8b: ${PIPESTATUS[@]}"

# pipestatus with $?
false | true; echo "9: $? ${PIPESTATUS[@]}"
{ false; true; } | true; echo "10: ${PIPESTATUS[@]}"
false | false | true | false; echo "11: ${PIPESTATUS[@]} $?"

# pipestatus and-or
true && false; echo "a: ${PIPESTATUS[@]}"
false || true; echo "b: ${PIPESTATUS[@]}"
true | false && echo x; echo "c: ${PIPESTATUS[@]}"

# pipestatus pipefail
set -o pipefail; false | true; echo "${PIPESTATUS[@]} $?"; true | false | true; echo "${PIPESTATUS[@]} $?"

# pipestatus subst
x=$(false | true); echo "${PIPESTATUS[@]}"
false; y=$(true | false); echo "${PIPESTATUS[@]} $?"

# pipestatus subshell
( false | true; echo "in: ${PIPESTATUS[@]}" ); echo "out: ${PIPESTATUS[@]}"

# funcname
echo "15: [${FUNCNAME[@]}]"
g() { echo "16: ${FUNCNAME[@]} | ${#FUNCNAME[@]}"; h; }; h() { echo "17: ${FUNCNAME[@]} | ${BASH_LINENO[@]}"; }; g

# funcname index
f() { echo "${FUNCNAME[0]} ${FUNCNAME[1]} ${FUNCNAME}"; }; g() { f; }; g

# bash_source top
echo "19: ${BASH_SOURCE[@]//"$0"/SCRIPT} ${BASH_LINENO[@]}"

# bash_source funcs
h() { echo "${BASH_SOURCE[@]//"$0"/SCRIPT} | ${BASH_LINENO[@]}"; }; g() { h; }; g

# bash_lineno
f() {
 echo "${BASH_LINENO[0]}"
}

f
f

# main guard
f() { [[ "${BASH_SOURCE[0]}" == "$0" ]] && echo main-guard-ok; }; f
[[ "${BASH_SOURCE[0]}" == "$0" ]] && echo top-ok

# versinfo
echo "${BASH_VERSINFO[0]} ${BASH_VERSINFO[1]} ${BASH_VERSINFO[2]} ${BASH_VERSINFO[3]} ${BASH_VERSINFO[4]} ${#BASH_VERSINFO[@]}"
declare -p BASH_VERSINFO | { read -r a b c; echo "$a $b"; }
(( BASH_VERSINFO[0] >= 4 )) && echo modern

# recursion
r() { (( $1 == 0 )) && { echo "${FUNCNAME[@]}"; return; }; r $(( $1 - 1 )); }; r 3

# subshell funcname
f() { ( echo "${FUNCNAME[@]}" ); echo "$(echo "${FUNCNAME[@]}")"; }; f

# trap in func
f() { trap 'echo "t: ${FUNCNAME[@]}"' EXIT; }; f

# versinfo readonly
( BASH_VERSINFO[0]=9 ) 2>/dev/null; echo "rc=$? ${BASH_VERSINFO[0]}"
