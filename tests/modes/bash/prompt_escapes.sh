# requires: env:HOSTNAME
# PS1 backslash escapes via ${PS1@P} (prompt.c). \s \V \D{} \d \t are left out:
# they differ by design or by clock.
cd /usr/share
for ps in '\u@\h:\w\$ ' '[\W]' 'a\[b\]c' '$PWD|\101|\n|\\|\e|\a' '\j \$' 'no-escape' '\q \z' '$(echo sub)' '$((1+2))'; do
  PS1=$ps; printf '<%s>\n' "${PS1@P}" | od -c
done
HOME=/usr; cd /usr/share/doc 2>/dev/null || cd /usr/share; PS1='\w \W'
echo "${PS1@P}"
for n in 1 2 3; do PROMPT_DIRTRIM=$n; echo "${PS1@P}"; done
cd /usr; echo "${PS1@P}"
cd /; echo "${PS1@P}"
