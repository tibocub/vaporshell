trap 'echo exit trap' EXIT
trap 'echo usr1 caught' USR1
kill -s USR1 $$
echo after-signal
trap - USR1
( trap 'echo sub exit' EXIT; echo in-sub )
f() { trap 'echo from func' EXIT; }; f
# Only the EXIT line is checked: bash also lists signals ignored on entry.
trap > trap.out; while IFS= read -r l; do case $l in *EXIT) echo "$l";; esac; done < trap.out
echo end
