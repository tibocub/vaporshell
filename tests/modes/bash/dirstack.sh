# pushd, popd, dirs.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
mkdir -p a b c; base=${PWD}
pushd a >/dev/null; pushd ../b >/dev/null; set -- $(dirs); echo $#; popd >/dev/null; echo ${PWD##*/}; popd >/dev/null; echo ${PWD##*/}
pushd -n a >/dev/null; echo ${PWD##*/}; set -- $(dirs); echo $#
dirs -c; set -- $(dirs); echo $#
cd /; pushd "$base/a" >/dev/null; pushd >/dev/null; echo ${PWD##*/}; pushd >/dev/null; echo ${PWD##*/}
dirs -c; cd "$base"; pushd a >/dev/null; pushd ../b >/dev/null; pushd +1 >/dev/null; echo ${PWD##*/}; set -- $(dirs); echo $#; pushd -0 >/dev/null; echo ${PWD##*/}
dirs -c; cd "$base"; pushd a >/dev/null; pushd ../b >/dev/null; popd +1 >/dev/null; set -- $(dirs); echo $#; echo ${PWD##*/}
dirs -c; popd 2>/dev/null; echo "rc=$?"; pushd /nonexistent 2>/dev/null; echo "rc=$?"; set -- $(dirs); echo $#
dirs -c; cd "$base"; pushd a >/dev/null; ( popd >/dev/null; echo ${PWD##*/} ); echo ${PWD##*/}; set -- $(dirs); echo $#
HOME=$base; cd "$base"; dirs; dirs -l | while read x; do [[ $x == $base ]] && echo long-form; done
dirs -c; pushd a >/dev/null; dirs -v | while read n d; do echo "$n ${d#$base}"; done
