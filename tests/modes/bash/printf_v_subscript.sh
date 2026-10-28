a=(x y)
printf -v 'a[1]' %s "it's"
printf -v a[3] %s z
i=5; printf -v "a[i]" %s q
printf '[%s]' "${a[@]}"; echo
declare -A m; printf -v 'm[k]' %s v; echo "${m[k]}"
