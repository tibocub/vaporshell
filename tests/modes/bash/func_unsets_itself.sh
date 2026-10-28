# a function may unset itself while it runs (bash_completion's lazy loaders)
f() { unset -f f; echo "in f: $FUNCNAME"; g; }
g() { echo "in g: ${FUNCNAME[*]}"; }
f; f 2>/dev/null; echo "again rc=$?"
