# Brace expansion.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
echo a{b,c,d}e; echo pre{1,2}post x{a,b}{c,d}y
echo {a,b{c,d},e}; echo {{1,2},{3,4}}
echo x{,y}z {a,}b {,}
echo {1..5} {5..1} {-2..2} {0..0}
echo {1..10..3} {10..1..4} {1..2..5}
echo {01..05} {008..011} {1..03}
echo {a..e} {e..a} {A..D} {a..k..3}
echo {a..3} {1..a} {aa..cc} {1.. 3}
echo {} {a} a{b}c {a,b {a b} {}}
echo "{a,b}" '{a,b}' {a,b}"{c,d}" \{a,b}
x=1; echo {$x,2}; a=b; echo {a,$a}_{1,2}
echo {$(echo x),y}
v=q; echo ${v}{1,2} $v{1,2}
echo /tmp/{a,b}/{c,d}.txt
for i in {1..3} x{a,b}; do echo $i; done
v={a,b}; echo "$v"; echo $v
echo {ec,ho}; {echo,hi}
echo {a,b}{1,2}{x,y}
echo {a\,b,c}
: > a1; : > a2; : > b1; echo {a,b}*
echo {5..1..2} {1..5..-2}
echo start{,-mid,-end}
set --  {1..200}; echo $#
