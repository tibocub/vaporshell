# bash: -n -e -E are options, escapes are off unless -e.
echo -n a; echo b
echo -e 'a\tb'
echo 'a\tb'
echo -E 'a\tb'
echo -ne 'x\n'; echo y
echo -e 'a\cb'; echo z
echo -e '\0101 \x41'
echo -en 'x'; echo y
echo -z
echo -- x
