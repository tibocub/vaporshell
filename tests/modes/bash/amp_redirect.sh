echo out &>f
cat f
{ echo o; echo e >&2; } &>>f
cat f
echo "rc=$?"
