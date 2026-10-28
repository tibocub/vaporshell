# the system's bash_completion loads, and its longopt completion works
bc=/usr/share/bash-completion/bash_completion
[ -r "$bc" ] || { echo "skip"; exit 0; }
PS1=x                       # profile.d guards test for an interactive shell
. "$bc" >/dev/null 2>&1; echo "load rc=$?"
COMP_WORDS=(ls --vers); COMP_CWORD=1; COMP_LINE="ls --vers"; COMP_POINT=${#COMP_LINE}
_comp_complete_longopt ls --vers ls
printf '[%s]\n' "${COMPREPLY[@]}"
t() { local cur prev words cword; _comp_get_words -n '=<>&' cur prev words cword; declare -p cur prev words cword; }
t
