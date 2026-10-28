# variables rc files and bash_completion test for
[ -n "$BASH" ] && echo "BASH set"
[ -x "$BASH" ] && echo "BASH is the shell"
case $SHLVL in ''|*[!0-9]*) echo "SHLVL bad: $SHLVL" ;; *) echo "SHLVL numeric" ;; esac
printf '[%s]\n' "$COMP_WORDBREAKS"
