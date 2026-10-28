# ${!name<op>word}: the operator applies to the variable name holds
o=v; v=hello
echo "${!o-x} ${!o:-x} ${!o:+alt} ${!o#he} ${!o%lo} ${!o/l/L} ${!o^} ${!o:1:2}"
unset v; echo "[${!o-def}] [${!o:-def}] [${!o:+alt}]"
v=; echo "[${!o-def}] [${!o:-def}]"
f() { echo "${!1} ${!1:0:1} ${!1-x}"; }
w=world; f w
words=(a b c); ref="words[1]"
echo "${!ref-} ${!ref}"
unset arr; ref="arr[0]"; echo "[${!ref-none}]"
