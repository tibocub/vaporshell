x=outer
( x=inner; echo $x )
echo $x
( cd /; pwd ); pwd | sed 's|.*|(cwd)|'
{ x=group; echo $x; }
echo $x
( exit 3 ); echo "status $?"
( echo a; echo b ) | tr a-z A-Z
{ echo g1; echo g2; } | wc -l
f() { ( return 4 ); echo "after $?"; }; f
( ( echo nested ) )
y=$(x=cmdsub; echo $x); echo "$y $x"
( false ) || echo "or ran"
! ( false ) && echo "negated"
(
  echo multi
  echo line
)
