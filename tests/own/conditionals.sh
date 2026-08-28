true && echo yes-and
false && echo should-not-print
false || echo yes-or
true || echo should-not-print
echo one; false && echo two; echo three
