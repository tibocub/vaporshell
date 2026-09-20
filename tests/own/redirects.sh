# requires: fork cmd:mktemp
t=$(mktemp -d); cd $t
echo one > f; echo two >> f; cat f
cat < f
echo err 2>&1 >/dev/null 1>&2 | cat
{ echo out; echo err >&2; } > both 2>&1; cat both
cat <<EOF2
heredoc $((1+1)) $(echo cmd) \$notvar
EOF2
cat <<'EOF2'
literal $((1+1)) $(echo cmd)
EOF2
cat <<-EOF2
	tabs stripped
	EOF2
cat <<"EOF2"
quoted delim $HOME
EOF2
v=val; cat <<EOF2 > h
$v
EOF2
cat h
exec 3>fd3; echo via3 >&3; exec 3>&-; cat fd3
exec 4<f; read a <&4; read b <&4; echo "$a $b"; exec 4<&-
echo x > /dev/null; echo "still here"
set -C; echo c1 > noclob; echo c2 > noclob 2>/dev/null; echo "status $?"; echo c3 >| noclob; set +C; cat noclob
{ echo a; echo b; } > grp; cat grp
(echo sub) > subf; cat subf
if true; then echo in-if; fi > iff; cat iff
for i in 1 2; do echo $i; done > loopf; cat loopf
cat < nonexistent 2>/dev/null; echo "status $?"
echo last > f2 && cat f2
cd /; rm -rf $t
