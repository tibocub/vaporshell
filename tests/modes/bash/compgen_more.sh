# compgen -V, -W with expansions, complete -a / -- / -D, compopt outside completion
compgen -V r -W "ab ac b" -- a; echo "rc=$? ${r[*]}"
compgen -V r -W "x" -- q; echo "rc=$? n=${#r[@]}"
input=$'--sort\n--version\n--help'
IFS=$'\n' compgen -V r -X '' -W '$input' -- --vers; echo "rc=$? ${r[*]}"
compgen -W 'red $(echo blue  x) ${HOME:+hh} $((1+2)) "q r"' -- ''
complete -a unalias; complete -A shopt shopt; complete -W 'a b' -- w
complete -p -- w unalias shopt
complete -F _fn -D; complete -p -D
complete -r -- w; complete -p w 2>/dev/null; echo "rc=$?"
compopt -o nospace 2>/dev/null; echo "rc=$?"
