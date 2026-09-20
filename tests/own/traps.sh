# requires: fork signals
trap 'echo exit trap' EXIT
trap 'echo usr1 caught' USR1
kill -s USR1 $$
echo after-signal
trap - USR1
( trap 'echo sub exit' EXIT; echo in-sub )
f() { trap 'echo from func' EXIT; }; f
trap
echo end
