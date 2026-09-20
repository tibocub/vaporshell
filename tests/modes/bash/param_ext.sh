# ${x:o:l} ${x/p/r} ${x^^} ${!x} ${x@Q} and friends.
x=abcdefghij; echo ${x:3} ${x:3:4} ${x:0:2} ${x:9} ${x:10}[]
echo ${x: -3} ${x: -3:2} ${x:(-4)} ${x: -20}[]
echo ${x:2:-3} ${x:0:-1} ${x: -5:-2}
y=abc; echo ${y:1:100} ${y:5:2}[]
n=2; echo ${x:n+1:n*2} ${x:$n:$((n+1))}
unset u; echo "[${u:1:2}]"
set -- a b c d e; echo "${@:2}"; echo "${@:2:2}"; echo "${@: -2}"
set -- a "b c" d; for p in "${@:2}"; do echo "[$p]"; done; for p in "${*:2}"; do echo "[$p]"; done
set -- a b c; echo "[${@:4}]" "[${@:3}]" "[${@:1:0}]"
h=hello_hello; echo ${h/ll/LL} ${h/l/L} ${h/zzz/Q} ${h/h*o/X}
echo ${h//l/L} ${h//hello/} ${h//[el]/_} ${h//o/}
echo ${h/#hello/X} ${h/%hello/X} ${h/#ello/X} ${h/%hell/X}
y=abc; echo ${y/#/pre-} ${y/%/-post} ${y//b/} [${y/}] ${y/b}
echo ${y/b/[&]} ${y//[ac]/(&)} ${y/b/\&} ${y/b/"&"}
s=a/b/c; echo ${s//\//-} ${s/\/b\//X}
z=abcabc; p=b; r=X; echo ${z//$p/$r} ${z/"$p"/Y}
g=abc123def; echo ${g/[0-9]*/N} ${g//[0-9]/#} ${g/?/_}
echo "[${u/a/b}]"
c=hello; echo ${c^} ${c^^} ${c^^[el]} ${c^l}
C=HELLO; echo ${C,} ${C,,} ${C,,[EL]} ${C,H}
m="hello World"; echo ${m^^} ${m,,} ${m^} ${m^^[lo]}
q='a b'; echo ${q@Q}; w="it's"; echo ${w@Q}; e=; echo [${e@Q}]; echo [${u@Q}]
t="hello world"; echo ${t@U} ${t@u} ${t@L}
a=value; nm=a; echo ${!nm}; k=1; set -- p q; echo ${!k}
nm=nope; echo "[${!nm}]"
a1=b; b=c; c=end; x1=a1; echo ${!x1}; y1=${!x1}; echo ${!y1}
ab1=1; ab2=2; abc=3; other=4; echo ${!ab*}; echo "${!ab@}"
for nn in "${!ab@}"; do echo "<$nn>"; done
echo "[${!zzz*}]"
w=hello; echo "${w:1:2}|${w/l/L}|${w^^}|${w@Q}"
x="a b c"; set -- ${x/b/X Y}; echo $#; set -- ${x:2}; echo $#
unset d; echo ${d:-dflt} ${d:=set} ${d:+alt}; echo ${d-nope}
x=abc; echo ${x:-1} ${nn2:-2}
x=abcabc; yy=b; echo ${x//${yy}/[${yy^^}]}
x=abc
echo ${x:1:-9}
echo after-error
unset mm
echo ${!mm}
echo after-indirect
