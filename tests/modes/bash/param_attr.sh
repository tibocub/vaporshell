# ${x@a} and ${x@A} report a variable's attributes.
x=1; echo "[${x@a}]"; export ex=2; echo "[${ex@a}]"; readonly ro=3; echo "[${ro@a}]"
echo "${x@A}"; echo "${ex@A}"
