# `select`: menu numbering, PS3, blank-line redisplay, invalid/out-of-range choices, EOF, break/continue,
# and the column layout bash computes from $COLUMNS (or the terminal, or 80).
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
# basic pick
( select x in a b c; do echo "picked $x ($REPLY)"; break; done
) <<'EOF_IN'
2
EOF_IN
# invalid then quit
( select x in a b c; do echo "picked=[$x] reply=[$REPLY]"; break; done
) <<'EOF_IN'
9
EOF_IN
# blank redisplay
( select x in a b c; do echo "x=[$x]"; break; done
) <<'EOF_IN'

1
EOF_IN
# eof immediately
( select x in a b; do echo unreachable; done; echo "after: x=[$x] REPLY=[$REPLY] rc=$?"
) < /dev/null
# empty list
( select x in; do echo unreachable; done; echo "done rc=$?"
) < /dev/null
# no in clause uses positional
( select x; do echo "$x"; break; done
) <<'EOF_IN'
2
EOF_IN
# continue then break
( select x in a b; do echo "x=$x"; [ "$x" = a ] && continue; break; done
) <<'EOF_IN'
1
2
EOF_IN
# PS3 custom
( PS3="pick> "; select x in a b; do echo got=$x; break; done
) <<'EOF_IN'
1
EOF_IN
# negative number
( select x in a b c; do echo "x=[$x] r=[$REPLY]"; break; done
) <<'EOF_IN'
-1
EOF_IN
# zero
( select x in a b c; do echo "x=[$x] r=[$REPLY]"; break; done
) <<'EOF_IN'
0
EOF_IN
# non numeric
( select x in a b c; do echo "x=[$x] r=[$REPLY]"; break; done
) <<'EOF_IN'
abc
EOF_IN
# trailing/leading ws valid
( select x in a b; do echo "x=[$x] r=[$REPLY]"; break; done
) <<'EOF_IN'
  1  
EOF_IN
# custom IFS ignored for REPLY
( IFS=:; select x in a b; do echo "x=[$x] r=[$REPLY]"; break; done
) <<'EOF_IN'
a:1:b
EOF_IN
# loop twice then eof
( select x in a b; do echo "n=$x"; done
) <<'EOF_IN'
1
2
EOF_IN
# break status
( select x in a b; do echo hi; break; done; echo "rc=$?"
) <<'EOF_IN'
1
EOF_IN
# nested select
( select x in a b; do select y in c d; do echo "$x/$y"; break; done; break; done
) <<'EOF_IN'
1
1
EOF_IN
# no items after in with vars
( set -- p q r; select x; do echo "$x"; break; done
) <<'EOF_IN'
3
EOF_IN
# column layout n=12 cols80
( COLUMNS=80; select x in one two three four five six seven eight nine ten eleven twelve; do break; done
) <<'EOF_IN'
1
EOF_IN
# column layout n=6 cols30
( COLUMNS=30; select x in aa bb cc dd ee ff; do break; done
) <<'EOF_IN'
1
EOF_IN
# column layout n=9 cols40
( COLUMNS=40; select x in a b c d e f g h i; do break; done
) <<'EOF_IN'
1
EOF_IN
# single column narrow
( COLUMNS=10; select x in one two three four five six seven eight nine ten eleven twelve; do break; done
) <<'EOF_IN'
1
EOF_IN
# wide reverts to single
( COLUMNS=200; select x in aa bb cc dd ee ff; do break; done
) <<'EOF_IN'
1
EOF_IN
# default columns no var
( unset COLUMNS; select x in a b; do break; done
) <<'EOF_IN'
1
EOF_IN
