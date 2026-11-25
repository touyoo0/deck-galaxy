#!/bin/bash
# Samples the running game with perf for N seconds (default 25; read only, no
# change to the game), then prints the share of each thread and the busiest
# functions per thread. Start it once a heavy fight is on. Needs perf on the Deck.
#   tools/bench/perf_game.sh [SECONDS]
# D      the folder for the profile (default ~/rg-monitor)
# CALLS=1  keep the call stacks (dwarf, 150 Hz): big files, but it shows who
#        calls malloc and memcpy (perf script -i FILE.data lists them)
N=${1:-25}
D=${D:-$HOME/rg-monitor}
PID=$(pgrep -x roguegalaxy | head -1)
[ -z "$PID" ] && { echo "game not running"; exit 1; }
mkdir -p "$D"
O=$D/perf-$(date +%H%M%S)
if [ -n "$CALLS" ]; then
  perf record -F 150 --call-graph dwarf,16384 -s -p "$PID" -o "$O.data" -- sleep "$N" >/dev/null 2>&1
else
  perf record -F 499 -s -p "$PID" -o "$O.data" -- sleep "$N" >/dev/null 2>&1
fi
perf report -i "$O.data" --stdio --no-children --sort pid -g none 2>/dev/null | grep -v "^#" | grep -v "^$" | head -10 > "$O.threads.txt"
perf report -i "$O.data" --stdio --no-children --sort comm,sym -g none 2>/dev/null | grep -v "^#" | grep -v "^$" | head -60 > "$O.top.txt"
cat "$O.threads.txt"
echo "$O.data  $O.threads.txt  $O.top.txt"
