# coproc [NAME] command: a background process with pipes to its stdin (NAME[1]) and from its stdout
# (NAME[0]), plus NAME_PID. NAME is only recognized ahead of a compound command, exactly matching bash's
# own grammar -- `coproc mycp cat` is the unnamed two-word simple command `mycp cat`, not NAME=mycp.
# A plain external command works the same way `cmd &` does without fork.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
# plain external command unnamed
( coproc cat; echo x >&${COPROC[1]}; read -u ${COPROC[0]} l; echo "got:$l"; kill $COPROC_PID 2>/dev/null
)
# two-word body is one unnamed simple command
( coproc mycp cat 2>&1; echo rc=$?
)
# invalid name falls back to simple
( coproc 1bad cat 2>&1; echo rc=$?
)
