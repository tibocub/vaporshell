# bash: aliases are not expanded when not interactive.
alias hi='echo aliased'
hi 2>/dev/null; echo "rc=$?"
alias
