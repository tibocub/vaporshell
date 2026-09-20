# requires: regex
# [[ =~ ]]: POSIX extended regular expressions (needs regcomp).
[[ abc123 =~ [0-9]+ ]] && echo re; [[ abc =~ ^b ]] || echo nore
p="a.c"; [[ abc =~ "$p" ]] || echo re-quoted-literal; [[ abc =~ $p ]] && echo re-unquoted
