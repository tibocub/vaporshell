case $HOSTTYPE in ?*) echo hosttype;; esac
case $MACHTYPE in ${HOSTTYPE}-*) echo machtype;; esac
[ "$BASHPID" = "$$" ] && echo bashpid-is-pid
[ $((EPOCHSECONDS - $(date +%s))) -le 1 ] && echo epochseconds
case $EPOCHREALTIME in *.??????) echo realtime;; esac
[ "$SRANDOM" != "$SRANDOM" ] && echo srandom-changes
echo "$SHELLOPTS"
set -e -u
echo "$SHELLOPTS"
shopt -s nullglob
case $BASHOPTS in *nullglob*) echo bashopts-has;; esac
echo "[$PS4]"
a=(x y); a=z env | grep '^a='; echo "${a[@]}"
