# trap on ERR, DEBUG, RETURN.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
two() { return 2; }; trap 'echo ERR:$?' ERR; false; echo a; true; two; echo b; trap - ERR
trap 'echo ERR' ERR; if false; then :; fi; false || true; ! true; true && false; echo end; trap - ERR
trap 'echo ERR' ERR; ( false ); echo after; f() { false; }; f; echo end; trap - ERR
trap 'echo ERR' ERR; false; trap - ERR; false; echo reset
trap 'echo ERR:$?' ERR; ( exit 3 ); echo end2; trap - ERR
trap 'echo D' DEBUG; echo a; echo b; trap - DEBUG
trap 'echo D' DEBUG; f() { echo in; echo in2; }; f; echo end; trap - DEBUG
trap 'echo D' DEBUG; for i in 1 2; do echo $i; done; trap - DEBUG
trap 'echo D' DEBUG; trap - DEBUG; echo quiet
f() { trap 'echo R' RETURN; echo in-f; }; f; echo after-f; trap - RETURN
echo 'echo sourced' > s.sh; trap 'echo R' RETURN; . ./s.sh; echo end-src; trap - RETURN
g() { echo g-body; }; trap 'echo R' RETURN; g; echo top-level-return-not-fired; trap - RETURN
trap 'echo x' err; trap 2>/dev/null > t.out; while IFS= read -r l; do case $l in *ERR) echo "$l";; esac; done < t.out; trap - ERR
trap 'echo e' ERR; trap 'echo d' DEBUG; trap 'echo r' RETURN; trap > t.out; trap - ERR DEBUG RETURN
while IFS= read -r l; do case $l in *' ERR'|*' DEBUG'|*' RETURN') echo "$l";; esac; done < t.out   # not signals inherited as ignored
