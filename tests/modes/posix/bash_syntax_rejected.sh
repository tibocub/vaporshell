# POSIX mode has none of bash's syntax extensions: each is rejected the way
# dash rejects it (a command-not-found, a syntax error or a bad substitution).
t() { ( eval "$1" ) 2>/dev/null; echo "rc=$?"; }
t '[[ a == a ]]'
t 'if [[ 1 == 1 ]]; then echo yes; fi'
t '(( 1 ))'
t 'for ((i=0;i<2;i++)); do echo $i; done'
t 'function f { echo hi; }; f'
# `time` is not a keyword in POSIX mode, so this is a lookup of a command named
# time: an empty PATH keeps the result independent of a host that has GNU time.
t 'PATH=/nonexistent-zz; time true'
t 'case a in a) echo 1;& b) echo 2;; esac'
t 'cat <<< hi'
t 'echo {a,b} {1..3}'
t 'echo x{1..3}y'
t "echo \$'a\\tb'"
t 'echo $"hi"'
t 'x=abc; echo ${x:1:2}'
t 'x=abc; echo ${x/b/X}'
t 'x=abc; echo ${x^^}'
t 'x=abc; n=x; echo ${!n}'
t 'x=abc; echo ${x@Q}'
t 'shopt -s nullglob'
t 'pushd /'
t 'dirs'
t "trap 'echo e' ERR"
t "trap 'echo d' DEBUG"
t 'echo *.nomatch'
t 'echo @(a|b)'
t '( (echo nested) )'
