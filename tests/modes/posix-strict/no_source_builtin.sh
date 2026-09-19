# `source` is a bash builtin; POSIX only has `.`
if command -v source >/dev/null 2>&1; then echo "has source"; else echo "no source"; fi
if command -v . >/dev/null 2>&1; then echo "has dot"; else echo "no dot"; fi
