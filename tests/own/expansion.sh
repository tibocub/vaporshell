# requires: cmd:sed env:HOME
unset u; e=; s=hello
echo "${u-default} ${e-default} ${e:-default} ${s:-default}"
echo "${u+alt} ${e+alt} ${e:+alt} ${s:+alt}"
echo "${u=assigned}" "$u"
echo ${#s} ${#u}
p=/usr/local/lib/libfoo.so.1.2
echo ${p#*/} ${p##*/} ${p%.*} ${p%%.*}
echo ${p%/*} ${p#/usr}
echo $((1+2*3)) $(( (1+2)*3 )) $((10/3)) $((10%3)) $((-5+2)) $((2<3)) $((1<<4))
x=5; echo $((x+1)) $((x*x)) $((x>3?100:200))
i=0; $((i+=1)) 2>/dev/null; echo $((i+=1)) $i
echo `echo tick` $(echo dollar) "$(echo "quoted  spaces")"
echo $(echo $(echo nested))
echo "a$(echo b)c" 'lit$(x)'
echo ${HOME:+home set}
v='a*b'; echo "$v" $v
echo "${s}world" "$s"world
echo "\$s" '\$s' "a\\b"
echo ~ ~/x | sed "s|$HOME|HOME|g"
w="  lead trail  "; echo "[$w]" [$w]
echo $((0x10)) $((010))
