echo "a b c" | { read x y z; echo "$x|$y|$z"; }
echo "a b c d" | { read x y; echo "$x|$y"; }
echo "  lead" | { read x; echo "[$x]"; }
printf 'l1\nl2\n' | { read a; read b; echo "$a $b"; }
printf 'no newline' | { read a; echo "[$a] status=$?"; }
echo 'back\slash' | { read -r x; echo "$x"; }
echo 'a:b:c' | { IFS=: read p q r; echo "$p $q $r"; }
read v <<EOF
from heredoc
EOF
echo "$v"
while read line; do echo "got $line"; done <<EOF
one
two
EOF
