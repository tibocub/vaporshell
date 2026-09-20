# $LINENO: absolute script lines, through functions, heredocs and eval.
echo $LINENO
echo $LINENO
f() {
  echo in-f $LINENO
}
f
if true; then
  echo in-if $LINENO
fi
cat <<EOT >/dev/null
x
EOT
echo after-heredoc $LINENO
eval "echo eval-\$LINENO
echo eval2-\$LINENO"
echo $((LINENO + 1))
