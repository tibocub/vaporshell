# the blanks of an unquoted ${v:+word} operand separate fields; quotes protect
c=--vers; d="a b"
show() { printf '<%s>' "$@"; echo; }
show ${c:+-- "$c"}
show ${c:+x "$d" y}
show ${c:+x $d y}
show ${c:+"$d"}
show ${unsetvar-- "$d"}
show "${c:+-- $d}"
