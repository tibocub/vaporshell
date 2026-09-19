f() { break; }
for i in 1 2; do echo $i; f; echo after$i; done
g() { for j in a b; do break 2; done; echo still-in-g; }
for i in 1 2; do g; echo after$i; done
for i in 1 2; do echo $i; eval break; echo not-printed; done
echo end
