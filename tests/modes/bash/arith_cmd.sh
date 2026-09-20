# (( )) and for (( )).
(( 3 > 2 )) && echo y; (( 0 )) || echo zero
x=5; (( x++ )); echo $x; (( x = x * 2 )); echo $x
(( 1 )); echo "rc=$?"; (( 0 )); echo "rc=$?"; (( 1+1 == 2 )); echo "rc=$?"
a=3; b=4; (( c = a * b )); echo $c; (( a += 2 )); echo $a
for ((i=0; i<3; i++)); do echo $i; done
for ((i=0;i<10;i++)); do [ $i -eq 2 ] && continue; [ $i -eq 5 ] && break; echo $i; done
i=0; for (( ; i<2; )); do echo $i; i=$((i+1)); done
for ((i=0,j=10; i<3; i++,j--)); do echo $i $j; done
( (echo nested-subshell) ); ((echo a; echo b) | cat)
