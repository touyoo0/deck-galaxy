#!/bin/sh
# Compares two binaries of the game by alternating their runs (A, B, A, B...), so that
# what drifts on the machine (heat, background load, the SD card) hits both alike.
#
#   tools/bench/deck/ab.sh SCENARIO BIN_A BIN_B [PAIRS=3]
#
# BIN_A and BIN_B are executables (copies of build/game/src/roguegalaxy, kept in
# $BENCH_HOME/bin by name); their runs are labelled by those names, then compared
# (compare.py A B). Environment as run.sh's (BENCH_UNPACED=1 BENCH_HIDDEN=1 ...).
here=$(dirname "$(readlink -f "$0")")
BENCH_HOME=${BENCH_HOME:-/run/media/deck/SD/Recomp/RogueBench}
scenario=$1; a=$2; b=$3; pairs=${4:-3}
[ -n "$b" ] || { sed -n '2,10p' "$0"; exit 2; }
i=1
while [ "$i" -le "$pairs" ]; do
    for name in "$a" "$b"; do
        bin="$BENCH_HOME/bin/$name"; [ -x "$bin" ] || bin=$name
        # A run's files are numbered by its own repeat: keep each pair's apart.
        GAME_BIN=$bin "$here/run.sh" "$scenario" "$name.$i" 1 >/dev/null 2>&1
        grep -E ' fps ' "$BENCH_HOME/runs/$name.$i/$scenario.1.txt" | sed "s/^/$name #$i: /"
    done
    i=$((i + 1))
done
# One label per binary: gather the pairs' json files.
for name in "$a" "$b"; do
    rm -rf "$BENCH_HOME/runs/$name"; mkdir -p "$BENCH_HOME/runs/$name"
    j=1
    while [ "$j" -le "$pairs" ]; do
        cp "$BENCH_HOME/runs/$name.$j/$scenario.1.json" "$BENCH_HOME/runs/$name/$scenario.$j.json"
        j=$((j + 1))
    done
done
python3 -I "$here/compare.py" "$a" "$b" "$scenario"
