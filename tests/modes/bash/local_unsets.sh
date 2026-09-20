# bash: `local v` makes v unset inside the function.
v=out
f() { local v; echo "[${v-unset}]"; }
f; echo "$v"
