# bash arithmetic extensions: **, comma, base#n, ++/--.
echo $((2**10)) $((-2**2)) $((2**3**2))
echo $((a=5, b=a*2, a+b))
echo $((2#101)) $((16#ff)) $((36#zz))
echo $(((1,2)+3))
x=5; echo $((x++)) $((++x)) $((x--)) $((--x)) $x
y=3; echo $((y**2 + y++))
echo $((0x1f)) $((010)) $((1?2:3))
( echo $((2**-1)) ) 2>/dev/null; echo "rc=$?"
