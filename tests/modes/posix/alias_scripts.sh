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
# The listing's quoting differs between shells (dash 0.5.13 quotes the whole name=value);
# what POSIX requires is that it can be read back, so read it back.
alias >al.out; unalias -a
while IFS= read -r l; do eval "alias $l"; done <al.out
ll x; sh_a; wc -l <al.out | tr -d ' '
unalias hi
hi 2>/dev/null; echo "rc=$?"
alias e='echo'; e "quoted alias name still fine"
\e hidden 2>/dev/null; echo "rc=$?"
