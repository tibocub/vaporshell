#!/bin/sh
# tests/nuttx-check.sh -- build vaporshell for vaporOS (NuttX) and test it there.
#
#   sh tests/nuttx-check.sh [--no-build|--build-only] [--full] [--verbose] [--jobs N]
#
#   (no option)  rebuild what changed (fast: `make` in the NuttX tree, which
#                only recompiles vaporshell's changed files), then test.
#                Falls back to a full vaporOS build if there is no configured
#                tree yet, or if vaporOS's build scripts changed since.
#   --full       always do the full vaporOS build (`dev.mk build`: distclean,
#                reconfigure, rebuild everything -- minutes).
#   --no-build   test the tree as it is.
#   --build-only build, but run no tests (`make build-vaporos`).
#   --verbose    stream the build output instead of keeping it in a log.
#   --jobs N     simulators to run at once for the differential tests.
#
# It then runs, in order: the global-symbol collision check, the simulator
# smoke test, and every differential suite (tests/reference/run-differential.py
# --all-suites) -- the same four suites the Linux build runs, against bash and
# dash.
#
# Environment:
#   VAPOROS_DIR  the vaporOS checkout      (default: ../vaporOS)
#   NUTTX_DIR    the NuttX tree it builds  (default: ../nuttx)
#   BOARD        vaporOS board for --full  (default: nsh)
#   ONLY         run only tests whose file name contains this
#   BASH_REF, DASH_REF   the reference shells (see tests/run-suites.sh)
#
# Exit status: 0 all passed, 1 something failed, 3 no vaporOS workspace here
# (tests/check-all.sh reports that as skipped rather than failed).
#
# Why the build runs with make's own state stripped from the environment: this
# script is normally started by `make`, and NuttX's build is a make of its own.
# Inheriting MAKEFLAGS (-j, the jobserver, -w...) from the outer one makes the
# inner build warn and misbehave in ways it does not when run by hand.

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$HERE")
. "$HERE/lib.sh"

BUILD=incremental
BUILD_ONLY=
VERBOSE=
JOBS=${NUTTX_JOBS:-1}
while [ $# -gt 0 ]; do
    case $1 in
        --no-build) BUILD=none ;;
        --build-only) BUILD_ONLY=1 ;;
        --full)     BUILD=full ;;
        --verbose|-v) VERBOSE=1 ;;
        --jobs)     shift; JOBS=$1 ;;
        *) echo "usage: $0 [--no-build|--build-only] [--full] [--verbose] [--jobs N]" >&2; exit 2 ;;
    esac
    shift
done

abs() { (cd "$1" 2>/dev/null && pwd); }
VAPOROS_DIR=$(abs "${VAPOROS_DIR:-$ROOT/../vaporOS}")
NUTTX_DIR=$(abs "${NUTTX_DIR:-$ROOT/../nuttx}")
BOARD=${BOARD:-nsh}

if [ -z "$VAPOROS_DIR" ] || [ ! -f "$VAPOROS_DIR/dev.mk" ] || [ -z "$NUTTX_DIR" ]; then
    echo "vaporOS workspace not found (VAPOROS_DIR='${VAPOROS_DIR:-../vaporOS}', NUTTX_DIR='${NUTTX_DIR:-../nuttx}')."
    echo "Set VAPOROS_DIR and NUTTX_DIR, or see vaporOS's setup.sh."
    exit 3
fi

if [ -z "$VS_TALLY" ]; then
    VS_TALLY=$(mktemp "${TMPDIR:-/tmp}/vaporshell-tally.XXXXXX") || exit 2
    export VS_TALLY
    own_tally=1
    trap 'rm -f "$VS_TALLY"' EXIT INT TERM
fi

step() { printf '\n%s==> %s%s\n' "$C_BOLD" "$1" "$C_RESET"; }

# Runs a command with make's inherited state removed (see above).
nested()
{
    env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL -u MAKEOVERRIDES -u MAKE_TERMOUT -u MAKE_TERMERR "$@"
}

ncpu() { nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1; }

# ---- build ---------------------------------------------------------------

if [ "$BUILD" != none ]; then
    if [ "$BUILD" = incremental ]; then
        if [ ! -f "$NUTTX_DIR/.config" ] || [ ! -x "$NUTTX_DIR/nuttx" ]; then
            echo "no configured NuttX tree in $NUTTX_DIR: doing a full build"
            BUILD=full
        elif [ "$VAPOROS_DIR/scripts/build.sh" -nt "$NUTTX_DIR/.config" ]; then
            echo "vaporOS's build script is newer than the NuttX configuration: doing a full build"
            BUILD=full
        fi
    fi

    mkdir -p "$ROOT/build"
    LOG="$ROOT/build/nuttx-build.log"
    start=$(date +%s)
    step "building vaporOS ($BUILD${BUILD:+ build}) -- log: $LOG"

    if [ "$BUILD" = full ]; then
        # BOARD=..., not a `sim:nsh` goal: dev.mk turns BOARD into sim:$BOARD
        # itself, and an extra goal makes make fail *after* a good build.
        set -- sh -c 'cd "$1" && shift && exec "$@"' sh "$VAPOROS_DIR" make -f dev.mk build "BOARD=$BOARD"
    else
        # NuttX's simulator link does not depend on the archives it links, so
        # an incremental `make` recompiles vaporshell into libapps.a and then
        # leaves the old `nuttx` binary in place -- and we would test stale
        # code. Removing the binary forces the (few-second) relink.
        rm -f "$NUTTX_DIR/nuttx"
        set -- sh -c 'cd "$1" && shift && exec "$@"' sh "$NUTTX_DIR" make -j"$(ncpu)"
    fi

    stamp="$ROOT/build/.nuttx-build-stamp"
    touch "$stamp"

    if [ -n "$VERBOSE" ]; then
        nested "$@"
        brc=$?
    else
        nested "$@" >"$LOG" 2>&1
        brc=$?
    fi

    elapsed=$(($(date +%s) - start))
    # A build that "succeeded" without producing a fresh binary is a failure
    # too: the tests below would be running whatever was there before.
    if [ "$brc" = 0 ] && [ -x "$NUTTX_DIR/nuttx" ] && [ ! "$NUTTX_DIR/nuttx" -nt "$stamp" ]; then
        echo "the build finished but $NUTTX_DIR/nuttx was not rebuilt"
        brc=1
    fi
    if [ "$brc" != 0 ] || [ ! -x "$NUTTX_DIR/nuttx" ]; then
        printf '%sbuild FAILED%s after %ss (exit %s)\n' "$C_RED" "$C_RESET" "$elapsed" "$brc"
        if [ -z "$VERBOSE" ]; then
            # NuttX redraws progress with \r and ANSI codes; make it readable.
            clean=$(sed 's/\x1b\[[0-9;]*[A-Za-z]//g; s/\r/\n/g' "$LOG")
            echo "$clean" | grep -E 'error|Error|undefined|No rule' | head -20
            echo "... last lines of $LOG:"
            echo "$clean" | tail -15
        fi
        tally_group "NuttX build" 0 1
        tally_fail "NuttX build" "$BUILD build"
        [ -n "$own_tally" ] && { echo; tally_report "$VS_TALLY" 1; }
        exit 1
    fi

    printf '%sbuilt%s in %ss\n' "$C_GREEN" "$C_RESET" "$elapsed"
    tally_group "NuttX build" 1 0
fi

if [ ! -x "$NUTTX_DIR/nuttx" ]; then
    echo "no simulator binary at $NUTTX_DIR/nuttx (build first)"
    exit 1
fi

if [ -n "$BUILD_ONLY" ]; then
    [ -n "$own_tally" ] || true
    exit 0
fi

# ---- symbol collisions -----------------------------------------------------

step "global symbol collisions with the rest of the NuttX link"
if out=$(sh "$HERE/nuttx-symcheck.sh" "$NUTTX_DIR/staging" 2>&1); then
    echo "$out" | tail -2
    tally_group "NuttX symbol collisions" 1 0
else
    echo "$out"
    tally_group "NuttX symbol collisions" 0 1
    tally_fail "NuttX symbol collisions" "global names"
fi

# ---- smoke ------------------------------------------------------------------

step "simulator smoke test"
out=$(python3 "$HERE/nuttx-sim-smoke.py" "$NUTTX_DIR/nuttx" 2>&1)
smoke_ok=$(echo "$out" | grep -c '^ok ')
smoke_bad=$(echo "$out" | grep -c '^FAIL')
echo "$out" | grep -v '^ok ' | head -20
printf 'smoke: %s ok, %s failed\n' "$smoke_ok" "$smoke_bad"
tally_group "NuttX smoke" "$smoke_ok" "$smoke_bad"
echo "$out" | grep '^FAIL' | while IFS= read -r l; do tally_fail "NuttX smoke" "${l#FAIL  }"; done

# ---- differential suites -------------------------------------------------------

step "differential suites in the simulator (jobs: $JOBS)"
python3 "$HERE/reference/run-differential.py" --all-suites --brief --jobs "$JOBS" \
    ${ONLY:+--only "$ONLY"} --tally "$VS_TALLY" --nuttx-dir "$NUTTX_DIR"

if [ -n "$own_tally" ]; then
    echo
    tally_report "$VS_TALLY" 1
    exit $?
fi
exit 0
