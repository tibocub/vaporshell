# enable [-n] [-a|-p] [name...]: turning shell builtins on and off. A disabled builtin is
# invisible to command resolution, so the shell finds an external program of the same name.
# disable then reenable
( enable -n echo; echo hi; enable echo; echo hi
)
# disabled falls through
( enable -n echo; echo still works
)
# enable -p filters disabled
( enable -n printf; n=0; while IFS= read -r l; do case $l in "enable printf") n=$((n+1));; esac; done <<< "$(enable -p)"; echo $n
)
# enable -a shows disabled with -n
( enable -n cd; while IFS= read -r l; do case $l in *cd) echo "$l";; esac; done <<< "$(enable -a)"
)
# enable no args lists enabled only
( enable -n echo; n=0; while IFS= read -r l; do case $l in "enable echo") n=$((n+1));; esac; done <<< "$(enable)"; echo $n
)
# nonexistent builtin errors
( enable nosuchbuiltin123 2>/dev/null; echo rc=$?
)
# multiple names
( enable -n echo printf; n=0; while IFS= read -r l; do case $l in "enable echo"|"enable printf") n=$((n+1));; esac; done <<< "$(enable)"; echo $n
)
# re-enable via plain name
( enable -n cd; enable cd; while IFS= read -r l; do [ "$l" = "enable cd" ] && echo "$l"; done <<< "$(enable -a)"
)
# disabled builtin command not found still runs binary
( enable -n true; true; echo rc=$?
)

# a subshell's own enable/disable does not outlive it
( ( enable -n echo ); command -v echo
)
