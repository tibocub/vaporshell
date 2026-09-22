# Unquoted ${v:off}, ${v/p/r}, ${v^^}, ${v,,} and ${v@U} are split like any expansion: leading IFS whitespace
# starts no field, and a quoted result is one field even when it is empty.
w="  Z  Y"
for op in '${w:1}' '${w: -4}' '${w:0}' '${w/Q/}' '${w//Q/}' '${w^^}' '${w,,}' '${w@U}' '${w:9}' '${w#Q}' '${w%Q}' '${w}'
do
  eval "set -- $op"
  echo "$op n=$# first=[$1]"
  eval "set -- \"$op\""
  echo "  quoted n=$# len=${#1}"
done
IFS=:
v=":a:b:"
set -- ${v:0}; echo "$# [$1] [$2]"
set -- ${v/a/A}; echo "$# [$1] [$2]"
IFS=" :"
v=" : x : y :"
set -- ${v^^}; echo "$# [$1] [$2] [$3]"
set -- ${v:1}; echo "$# [$1] [$2] [$3]"
