# requires: async
# coproc with a compound command body ({ ...; }, ( ... )): the coprocess is the shell itself running
# that body, which needs a real fork -- like `{ ...; } &`, this only works where fork does.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
# named brace body
( coproc mycp { cat; }; echo hello >&${mycp[1]}; read -u ${mycp[0]} line; echo "got: $line"; kill $mycp_PID 2>/dev/null
)
# unnamed brace body
( coproc { cat; }; echo hi >&${COPROC[1]}; read -u ${COPROC[0]} line; echo "got: $line"; kill $COPROC_PID 2>/dev/null
)
# declare -p of array
( coproc mycp { cat; }; declare -p mycp | sed "s/[0-9]\{1,\}/N/g"; kill $mycp_PID 2>/dev/null
)
# subshell body
( coproc mycp ( cat ); echo x >&${mycp[1]}; read -u ${mycp[0]} l; echo "got:$l"; kill $mycp_PID 2>/dev/null
)
# multiple lines through
( coproc mycp { cat; }; printf "a\nb\n" >&${mycp[1]}; read -u ${mycp[0]} l1; read -u ${mycp[0]} l2; echo "$l1|$l2"; kill $mycp_PID 2>/dev/null
)
