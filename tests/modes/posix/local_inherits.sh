# dash: `local v` keeps the outer value visible.
v=out
f() { local v; echo "[${v-unset}]"; v=changed; }
f; echo "$v"
