# type sees aliases once they are enabled (they are not in a script by default)
shopt -s expand_aliases
alias ll='ls -l'
type ll
type -t ll
