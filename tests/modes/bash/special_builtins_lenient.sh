# In bash's default mode special builtins are not special where it matters.
readonly a=1
export a=2 2>/dev/null
echo "survived readonly export"
x=1 :
echo "[$x]"
:() { echo FUNC; }
:
echo "function beats special builtin"
eval "if" 2>/dev/null
echo "survived eval syntax error"
