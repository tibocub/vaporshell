# requires: path-lookup
# hash: remembering, forgetting and errors (listing format differs; see modes/).
hash ls; echo "rc=$?"
hash nosuchcmd_zz 2>/dev/null; echo "rc=$?"
hash -r; echo "rc=$?"
PATH=/nonexistent; hash ls 2>/dev/null; echo "rc=$?"
