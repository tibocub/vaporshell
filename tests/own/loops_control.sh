# requires: fork
for i in 1 2 3; do
  for j in a b c; do
    if [ $j = b ]; then continue; fi
    if [ $i = 2 ]; then break 2; fi
    echo "$i$j"
  done
done
echo after
i=0; while :; do i=$((i+1)); [ $i -ge 3 ] && break; done; echo $i
i=5; until [ $i -le 2 ]; do i=$((i-1)); done; echo $i
for i in 1 2 3; do [ $i = 2 ] && continue; echo $i; done
set -- p q r; for i; do echo $i; done
for i in; do echo never; done; echo "empty for ok"
while false; do echo never; done; echo "status $?"
for i in 1 2; do echo $i; done | tail -1
n=0; for a in 1 2; do for b in 1 2; do n=$((n+1)); continue 2; done; done; echo $n
