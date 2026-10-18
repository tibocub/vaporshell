# PS1 escapes that do not depend on the user, host or uid (so also on NuttX).
cd /
for ps in '[\W]' 'a\[b\]c' '$PWD|\101|\n|\\|\e|\a' 'no-escape' '\q \z' '$(echo sub)' '$((1+2))' '\w'; do
  PS1=$ps; printf '<%s>\n' "${PS1@P}" | od -c
done
HOME=/; PS1='\w \W'; echo "${PS1@P}"
