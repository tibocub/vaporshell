# printf: the POSIX core, identical in bash and dash.
printf '%s=%d\n' a 5 b 6
printf '%s\n' one two three
printf 'x%sy%dz\n'
printf '%5s|%-5s|%.2s|\n' ab cd efgh
printf '%x %o %X %#x %05d %+d\n' 255 8 255 255 42 7
printf '%c%c\n' abc xyz
printf '%b\n' 'a\tb\\n'
printf '\101\102\n'
printf '100%%\n'
printf '%d %d %d\n' 0x1f 010 "'A"
printf '%*d|%-*d|\n' 5 42 4 7
printf '%u\n' -1
printf '%s %s\n' onlyone
printf '%d\n' notanumber 2>/dev/null; echo "rc=$?"
printf 'a\nb\n'
printf '%s\n' ""
printf 'no newline'; echo
