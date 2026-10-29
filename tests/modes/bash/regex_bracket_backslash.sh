# a quoted ] or [ inside a bracket expression is a plain member (tar's bash completion parses --help with this)
l='  -A, --catenate, --concatenate    append'
[[ $l =~ ^[[:blank:]]+(--?([\]\[a-zA-Z0-9?=-]+)) ]] && echo "${BASH_REMATCH[1]}"
[[ 'a]b' =~ ^a[\]]b$ ]] && echo bracket-close
[[ 'a[b' =~ ^a[\[]b$ ]] && echo bracket-open
[[ 'ab' =~ ^a[^\]]$ ]] && echo negated
[[ 'a.b' =~ a\.b ]] && echo escaped-dot
[[ 'axb' =~ a\.b ]] || echo no-wildcard
