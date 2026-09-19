set -e
x=$(true); echo ok1
if x=$(false); then :; else echo ok2; fi
x=$(false) || echo ok3
x=$(false)
echo not-reached
