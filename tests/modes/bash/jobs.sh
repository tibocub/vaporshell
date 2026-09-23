# requires: wc-format
# Background jobs (cmd &): jobs, wait (job specs, -n, multiple operands), kill %N, disown, and the
# [N]+/- markers and status text (Running/Done/Exit N/a signal name) jobs prints. Job ids here count
# up forever and are never reused, unlike bash's; and, since bash's own +/- marker choice is itself
# non-deterministic once more than one job has already finished (its SIGCHLD reaping order isn't
# fixed), a case exercising that is left out rather than pinned to one arbitrary bash run.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
# basic two running
( sleep 1 & sleep 1 & jobs
)
# one done one running
( sleep 0.2 & sleep 1 & sleep 0.4; jobs
)
# wait with job spec
( sleep 0.1 & wait %1; echo rc=$?
)
# wait no args status
( false & wait; echo rc=$?
)
# jobs -l
( sleep 1 & jobs -l | sed "s/[0-9]\{2,\}/PID/"
)
# jobs -p count
( sleep 1 & jobs -p | wc -l
)
# three running markers
( sleep 1 & sleep 1 & sleep 1 & jobs
)
# disown removes from jobs
( sleep 1 & disown; jobs; echo "count=$(jobs | wc -l)"
)
# wait removes from table
( sleep 0.1 & wait %1; jobs; echo rc=$?
)
# wait -n picks any
( true & false & wait -n; echo "rc=$?"
)
# wait multiple specs last status
( false & true & wait %1 %2; echo rc=$?
)
# wait multiple specs last status2
( true & false & wait %1 %2; echo rc=$?
)
# wait bad jobspec
( wait %5 2>/dev/null; echo rc=$?
)
# wait bad pid
( wait 99999 2>/dev/null; echo rc=$?
)
# kill jobspec
( sleep 1 & kill %1; sleep 0.2; jobs
)
# done then running
( true & sleep 1 & sleep 0.1; jobs | tail -1
)
# running then done
( sleep 1 & true & sleep 0.1; jobs | head -1
)
# single running job
( sleep 1 & jobs
)
# signal terminated display
( sleep 1 & kill %1; sleep 0.2; jobs
)
