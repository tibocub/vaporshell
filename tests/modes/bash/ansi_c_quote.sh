# $'...' and $"..."
echo $'a\tb\nc'
x=$'\a\b\e\f\r\v\\'; echo ${#x}
echo $'it\'s' $'say \"hi\"'
[[ $'\101\x42\103' == ABC ]] && echo octal-hex
[[ $'\cA' == $'\x01' ]] && echo ctrl
echo $'a\zb'
echo pre$'x\ty'post "a"$'b'"c"
x=$'a b'; echo "$x"; set -- $'a b' c; echo $#
echo "$'a\tb'"
v=$'l1\nl2'; echo "$v"
case $'a\tb' in $'a\tb') echo match;; esac
echo [$''] [$'']x
: > 'a*'; echo $'a*'
echo $"hello world"; x=v; echo $"a $x b"
v=$"txt"; echo "$v"
