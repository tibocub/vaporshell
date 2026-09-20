# bash's computed variables and $_ .
[ -n "$BASH_VERSION" ] && echo version
[ "$RANDOM" -ge 0 ] && [ "$RANDOM" -le 32767 ] && echo random
RANDOM=42; a=$RANDOM; RANDOM=42; b=$RANDOM; [ "$a" = "$b" ] && echo seeded
SECONDS=100; [ "$SECONDS" -ge 100 ] && echo seconds
[ "$UID" -ge 0 ] && [ "$EUID" -ge 0 ] && echo ids
[ -n "$HOSTNAME" ] && echo host
[ -n "$OSTYPE" ] && echo ostype
echo one two three >/dev/null; echo "$_"
echo x > /dev/null; echo "$_"
RANDOM_SHADOW=1
[ -v RANDOM_SHADOW ] && echo v-works
