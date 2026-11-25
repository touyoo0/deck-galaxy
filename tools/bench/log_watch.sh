#!/bin/bash
# Archives a game folder's build/last-run.log (read only) and notes the time of
# each [gfx] line in $D/timeline.log, so that "it lagged at 21:42" finds its
# line, and a crash's log survives the next launch. Run it for the whole play
# with: systemd-run --user --unit=rg-monitor tools/bench/log_watch.sh
# GAME   the game folder (default: the current one), or G the log itself
# D      where the copies and timeline.log go (default ~/rg-monitor)
GAME=${GAME:-$PWD}
G=${G:-$GAME/build/last-run.log}
D=${D:-$HOME/rg-monitor}
mkdir -p "$D/runs"
prev=0; cur=""; seen=0
while true; do
  if [ -f "$G" ]; then
    s=$(stat -c %s "$G")
    if [ "$s" -lt "$prev" ] || [ -z "$cur" ]; then cur=$D/runs/$(date +%F_%H-%M-%S).log; seen=0; echo "=== new run -> $cur" >> $D/timeline.log; fi
    prev=$s
    cp -f "$G" "$cur"
    n=$(grep -c "^\[gfx\] [0-9.]* fps" "$G")
    if [ "$n" -gt "$seen" ]; then
      fps=$(grep "^\[gfx\] [0-9.]* fps" "$G" | tail -1 | cut -d" " -f2)
      echo "$(date +%T) gfxline=$n fps=$fps" >> $D/timeline.log; seen=$n
    fi
  fi
  sleep 2
done
