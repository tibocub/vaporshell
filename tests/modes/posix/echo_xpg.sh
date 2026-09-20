# dash: only a lone -n is an option; escapes are always interpreted.
echo -n a; echo b
echo -e 'a\tb'
echo 'a\tb'
echo -ne 'x\n'; echo y
echo 'a\cb'; echo z
echo '\0101 \101'
echo -- x
