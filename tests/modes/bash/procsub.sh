# requires: cmd:diff cmd:paste cmd:tr
# <(cmd) and >(cmd) (process substitution). Both run to completion and are materialized as a real
# temp file rather than a true concurrent pipe (this shell has no background-execution model to
# build that on, and NuttX has no fork at all) -- so the path itself differs from bash's /dev/fd/N
# and is not compared, and a >(cmd) consumer only runs once the command that used it is done; bash's
# own true-concurrency version can race on this (a `sleep` here gives it a fair chance to finish).
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
# basic input
( cat <(echo hello)
)
# diff same
( diff <(echo a) <(echo a) && echo same
)
# diff differ
( diff <(echo a) <(echo b); echo rc=$?
)
# paste two
( paste <(echo a) <(echo b)
)
# nested
( cat <(cat <(echo nested))
)
# with pipeline inside
( cat <(echo a; echo b | tr a-z A-Z)
)
# no expansion in dquotes
( echo "<(echo x)"
)
# exit status unaffected
( cat <(false); echo rc=$?
)
# no trailing newline preserved
( cat <(printf "no newline"); echo "|end"
)
# multi line content
( cat <(printf "a\nb\nc\n")
)
# as redirection target
( cat < <(echo redirtest)
)
# multiple substitutions same command
( cat <(echo one) <(echo two) <(echo three)
)
# output procsub basic
( echo hi | tee >(cat > procsub_out.txt) >/dev/null; sleep 1; cat procsub_out.txt
)
# output procsub direct write
( echo direct > >(cat > procsub_out2.txt); sleep 1; cat procsub_out2.txt
)
# multiple statements dont leak
( cat <(echo first); cat <(echo second)
)
# in a loop
( for i in 1 2 3; do cat <(echo "n=$i"); done
)
# command not found inside
( cat <(nonexistent_cmd_xyz) 2>/dev/null; echo rc=$?
)
# empty output
( cat <(true); echo "|end"
)
# in function
( f() { cat <(echo "in func"); }; f
)
# with variables
( x=hello; cat <(echo "$x world")
)
# wc on procsub
( wc -l < <(printf "a\nb\nc\n")
)
