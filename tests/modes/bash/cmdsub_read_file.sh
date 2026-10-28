# $(<file) reads the file itself
f=${TMPDIR:-/tmp}/vs-cmdsub-$$.txt
printf 'a b\nc\n\n\n' > "$f"
x=$(<"$f"); echo "[$x]"
y=$(< "$f"); echo "[$y]"
z=$(<$f); echo "[$z]"
echo "[$(cat <"$f")]"
echo "[$(<"$f" tr a-z A-Z)]"
rm -f "$f"
