# printf %(fmt)T: bash's strftime-backed time conversion -- the empty format, nesting, width/precision,
# arg reuse, -1/-2, and $SECONDS (which starts near 0, not the raw epoch).
#
# Pinned to UTC: NuttX has no timezone database and always resolves local time as UTC (see
# docs/bash-coverage.md), so without this the epoch-based cases below would only agree with the
# reference bash when the host running that reference also happens to be in UTC.
export TZ=UTC
# basic date
( printf "%(%Y-%m-%d)T\n" 1700000000
)
# time only
( printf "%(%H:%M:%S)T\n" 1700000000
)
# empty format
( printf "[%()T]\n" 1700000000
)
# epoch zero
( printf "%(%s)T\n" 0
)
# literal percent
( printf "%(%%Y)T\n" 0
)
# width
( printf "[%10(%Y)T]\n" 0
)
# left width
( printf "[%-10(%Y)T]\n" 0
)
# two specs
( printf "%(%Y)T %(%m)T %(%d)T\n" 0
)
# reuse across args
( printf "%(%Y)T\n" 0 1000000000 2000000000
)
# precision
( printf "[%.5(%Y-%m-%d)T]\n" 0
)
# nested parens
( printf "%((%Y))T\n" 0
)
# no args at all
( printf "%(%%s literal)T\n" >/dev/null; echo ok
)
# weekday
( printf "%(%A %B)T\n" 0
)
# mixed with other specs
( printf "%s %(%Y)T %d\n" hello 0 42
)
# negative epoch (pre-1970)
( printf "%(%Y-%m-%d)T\n" -86400
)
# v flag with T
( printf -v out "%(%Y)T" 0; echo "$out"
)
# large positive
( printf "%(%s)T\n" 99999999999
)
# SECONDS starts near zero
( s0=$SECONDS; sleep 1; s1=$SECONDS; (( s1 - s0 >= 1 && s1 - s0 <= 3 )) && echo ok )
