echo 'echo sourced-from-cwd' > s.sh
. s.sh 2>/dev/null
echo "not reached"
