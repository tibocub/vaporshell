# POSIX: aliases expand in scripts, on the line after they are defined.
alias hi='echo aliased'
hi there
alias ll='echo long '
alias x=y
alias y='echo second'
ll x
alias sh_a='echo a; echo b'
sh_a
alias loop='loop'
alias
unalias hi
hi 2>/dev/null; echo "rc=$?"
alias e='echo'; e "quoted alias name still fine"
\e hidden 2>/dev/null; echo "rc=$?"
