# complete, compopt, bind: there is no line editor in this shell for these to attach to, so they
# are accepted but (beyond complete's own spec registry, which a script can query back) inert.
# register function spec
( complete -F _foo mycmd; echo rc=$?
)
# list all empty
( complete -p 2>&1; echo rc=$?
)
# register and roundtrip
( complete -W "a b c" mycmd; complete -p mycmd; echo rc=$?
)
# remove then query errors
( complete -W "a b" mycmd; complete -r mycmd; complete -p mycmd 2>/dev/null; echo rc=$?
)
# query nonexistent errors
( complete -p nosuchcmd_xyz 2>/dev/null; echo rc=$?
)
# compopt outside completion errors
( compopt -o nospace 2>/dev/null; echo rc=$?
)
# bind basic warns but succeeds
( bind "\C-l:clear-screen" 2>/dev/null; echo rc=$?
)
# multiple names one spec
( complete -W "x y" cmd1 cmd2; complete -p cmd1; complete -p cmd2
)
