# FUNCNAME, BASH_SOURCE and BASH_LINENO across sourced files.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
# BASH_SOURCE without the script's own path (a runner may stage it under another name); the
# helper's own frame is entry 0
bs() { local e; for e in "${BASH_SOURCE[@]:1}"; do [[ $e == "$0" ]] && e=SCRIPT; printf '%s ' "${e##*/}"; done; }
# source frames
echo 'echo "s1: [${FUNCNAME[@]:-none}] $(bs)"; k() { echo "s2: ${FUNCNAME[@]} $(bs) | ${BASH_LINENO[@]}"; }; k' > src.sh
. ./src.sh
k

# source nested
echo 'inner() { echo "${FUNCNAME[@]}"; }; inner' > in.sh; echo '. ./in.sh; f2() { inner; }; f2' > out.sh
. ./out.sh

# source in function
echo 'echo "${FUNCNAME[@]} | $(bs)"' > s.sh
w() { . ./s.sh; }; w
