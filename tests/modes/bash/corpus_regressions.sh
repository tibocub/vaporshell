# Found by running real projects (tests/corpus): bats, bash-it, bash-preexec.
set -euo pipefail                       # -o inside a flag cluster
set -eET; set +ET; echo "$-" | tr -d 'chmBHs' | tr -d '\n'; echo
set -o keyword; set +o keyword
f() { echo "f $*"; }
declare -ft f                           # -t sets the trace attribute, prints nothing
export -f f; bash -c 'f from-child'     # exported functions reach children
trap 'echo winch' WINCH; kill -WINCH $$
shift 2>&1 || echo "shift rc=$? (silent)"
e=(); t=("${e[@]+x}"); echo "${#t[@]}"  # "${a[@]+w}" on an empty array: no word
t=("${nope[@]+${nope[@]}}"); echo "${#t[@]}"
# DEBUG/ERR traps: $BASH_COMMAND, $LINENO of compound commands, set -E / -T
trap 'echo "D: $BASH_COMMAND ($LINENO)"' DEBUG
for i in 1 2; do :; done
trap - DEBUG
trap 'echo "E $?: $BASH_COMMAND"' ERR
f2() { false; echo after; }
f2
set -E
f2
(false)
# frameworks (oh-my-bash, bash-it, bash-preexec) found these
alias -- ll='ls -l'; alias -- ll; unalias -- ll
type -aP sh | head -1 | grep -c /
shopt -s cdable_vars compat31 extdebug; d=/usr; (cd d; pwd)
bind "set completion-ignore-case on"; bind Space:magic-space
shopt -oq posix; echo "posix=$?"
echo ${ echo funsub; } "${| REPLY=reply; }"
printf ' a b\nc d  ' | { read -N 7 x y; echo "[$x][$y]"; }     # -N: delimiter not special, no splitting
