# tests/lib.sh -- shared by the test scripts. Source it; do not run it.
#
# Colours: on when stdout is a terminal, off for pipes and files, NO_COLOR
# turns them off, FORCE_COLOR=1 turns them on regardless.
#
# Tally: every test group appends a line to the file named by $VS_TALLY (if
# set), so a wrapper can print one grand total at the end instead of leaving
# you to add up per-group counts:
#
#   G <TAB> label <TAB> passed <TAB> failed <TAB> skipped
#   F <TAB> label <TAB> name of a failed test

if { [ -t 1 ] && [ -z "$NO_COLOR" ]; } || [ -n "$FORCE_COLOR" ]; then
    C_RED=$(printf '\033[91m')
    C_GREEN=$(printf '\033[92m')
    C_YELLOW=$(printf '\033[93m')
    C_BOLD=$(printf '\033[1m')
    C_RESET=$(printf '\033[0m')
else
    C_RED= C_GREEN= C_YELLOW= C_BOLD= C_RESET=
fi

tally_group()
{
    [ -n "$VS_TALLY" ] && printf 'G\t%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "${4:-0}" >>"$VS_TALLY"
    return 0
}

tally_fail()
{
    [ -n "$VS_TALLY" ] && printf 'F\t%s\t%s\n' "$1" "$2" >>"$VS_TALLY"
    return 0
}

# tally_failed FILE: how many failed tests the tally holds so far.
tally_failed()
{
    awk -F'\t' '$1 == "G" { f += $4 } END { print f + 0 }' "$1"
}

# tally_guard FILE LABEL STATUS FAILED_BEFORE: a step that exits non-zero has
# normally recorded the tests that failed. If it did not (the count is what it
# was before the step), it died before it could -- a crashed runner, a missing
# tool -- and the summary would read "0 failed" with the step simply absent. Record
# it as a failed group instead, so it shows in the totals and in the FAILED list.
tally_guard()
{
    [ "$3" -ne 0 ] || return 0
    [ "$(tally_failed "$1")" -eq "$4" ] || return 0
    printf 'G\t%s (exit %s: did not report its results)\t0\t1\t0\n' "$2" "$3" >> "$1"
    printf 'F\t%s\texited %s without reporting a failing test\n' "$2" "$3" >> "$1"
}

# tally_report FILE [table]: the failed tests, optionally a per-group table,
# and the TOTAL line. Returns non-zero if anything failed.
tally_report()
{
    awk -F'\t' -v red="$C_RED" -v green="$C_GREEN" -v yellow="$C_YELLOW" \
        -v bold="$C_BOLD" -v reset="$C_RESET" -v table="${2:-0}" '
    $1 == "G" { p += $3; f += $4; s += $5; n++
                lab[n] = $2; gp[n] = $3; gf[n] = $4; gs[n] = $5 }
    $1 == "F" { nf++; fl[nf] = $2 ": " $3 }
    END {
        if (table)
            for (i = 1; i <= n; i++)
                printf "  %-54s %4d passed  %s%4d failed%s  %4d skipped\n",
                       lab[i], gp[i], (gf[i] > 0 ? red : ""), gf[i],
                       (gf[i] > 0 ? reset : ""), gs[i]
        for (i = 1; i <= nf; i++)
            printf "%sFAILED%s  %s\n", red, reset, fl[i]
        printf "%s%sTOTAL: %d passed, %d failed", bold, (f > 0 ? red : green), p, f
        if (s > 0) printf ", %d skipped", s
        printf "  (%d groups)%s\n", n, reset
        exit (f > 0)
    }' "$1"
}
