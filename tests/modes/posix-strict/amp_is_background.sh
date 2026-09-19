# In POSIX mode "&>" is "&" then ">": the echo runs in the background
# unredirected, and the empty command after it just creates the file.
echo hi &>marker
wait
[ -e marker ] && echo "marker created"
