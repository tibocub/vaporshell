# "\$" in a prompt is a literal $ for the expansion step that follows (Fedora's colour prompt: \$${PROMPT_END:+...})
PS1='\$${USER:+x} \$${NOPE:+y}\$ '
echo "${PS1@P}"
PS1='[\u@\h \W]\$ '
[ "${PS1@P}" = "[$USER@$(hostname -s) ${PWD##*/}]\$ " ] && echo plain-ok
