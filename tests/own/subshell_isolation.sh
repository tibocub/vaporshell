# A subshell changes nothing the parent can see: variables, functions,
# positional parameters, options, traps, cwd, umask, file descriptors.
mkdir -p w && cd w      # a known directory name, wherever the test starts
x=1; export E=exported
( x=2; E=changed; y=new; echo "in: $x $E $y" ); echo "out: $x $E ${y-unset}"
f() { echo parent-f; }
( f() { echo sub-f; }; f; unset -f g 2>/dev/null ); f
g() { echo g-defined; }; ( unset -f g ); g
set -- a b c
( set -- x; shift; echo "in: $# $*" ); echo "out: $# $*"
( shift 2; echo "in: $#" ); echo "out: $#"
mkdir -p d1/d2
( cd d1/d2; echo "in: ${PWD##*/}" ); echo "out: ${PWD##*/}"
umask 022; ( umask 077; umask ); umask
( set -f; case $- in *f*) echo in-noglob;; esac ); case $- in *f*) echo leaked;; *) echo not-leaked;; esac
( trap 'echo sub-exit-trap' EXIT; echo in-sub ); echo after
trap 'echo parent-exit-trap' EXIT
( trap - EXIT; echo trap-cleared-in-sub )
# Only the EXIT line is checked: a bare `trap` also lists signals that were
# ignored on entry in bash (not in dash), which depends on how the suite was started.
trap > trap.out; while IFS= read -r l; do case $l in *EXIT) echo "$l";; esac; done < trap.out
trap - EXIT
exec 3>fd3.out; echo to3 >&3
( exec 3>other.out; echo lost >&3 ); echo "still3" >&3; exec 3>&-
cat fd3.out; [ -e other.out ] && echo other-exists
( exec 4>opened.out; echo x >&4 ); { echo test >&4; } 2>/dev/null; echo "rc=$?"
# exit, errexit and return only end the subshell
( exit 5 ); echo "rc=$?"
( set -e; false; echo not-reached ); echo "rc=$?"
h() { ( return 7 ); echo "after sub return: $?"; return 0; }; h
# nesting
( ( x=3; echo "inner $x" ); echo "mid $x"; x=4 ); echo "outer $x"
# command substitution
y=$(x=9; echo "cs $x"); echo "$y $x"
z=$(f); echo "$z"
w=$(echo $(echo deep-$(echo deeper))); echo "$w"
n=$(i=0; while [ $i -lt 2000 ]; do echo $i; i=$((i+1)); done | { c=0; while read _; do c=$((c+1)); done; echo $c; }); echo "big: $n"
big=$(i=0; while [ $i -lt 400 ]; do echo "line number $i of a long output"; i=$((i+1)); done); echo "${#big}"
r=$(exit 4); echo "rc=$?"
q=$(echo a; { echo b >&2; } 2>/dev/null; echo c); echo "$q"
# exec inside a subshell runs the command and ends only the subshell
( exec echo from-exec; echo not-reached ); echo "after-exec"
# local, aliases, IFS
k() { local v=inner; ( v=sub; echo "sub $v" ); echo "fn $v"; }; v=outer; k; echo "$v"
alias zz='echo aliased'; ( unalias zz 2>/dev/null; echo unaliased ); alias zz >/dev/null && echo alias-kept
( IFS=:; set -- a:b; echo "$*" ); echo "$*"
