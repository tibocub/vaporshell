x=b
case $x in
a) echo matched-a ;;
b) echo matched-b ;;
*) echo matched-default ;;
esac
y=z
case $y in
a) echo matched-a ;;
*) echo matched-default ;;
esac
