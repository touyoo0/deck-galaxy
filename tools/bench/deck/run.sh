#!/bin/sh
# One scripted, repeatable performance run on the Steam Deck.
#
#   tools/bench/deck/run.sh SCENARIO LABEL [REPEAT=1]
#
# Loads the scenario's save state, replays its scripted pad input counted in
# the game's pad reads from the moment the state is loaded (so the scene is
# the same whatever the boot took), stops after its last read, and writes
#   $BENCH_HOME/runs/LABEL/SCENARIO.<n>.log   the game's log
#   $BENCH_HOME/runs/LABEL/SCENARIO.<n>.csv   the present log (one line a frame)
# then prints the summary (analyze.py). Compare two labels with compare.py.
#
# Never touches the player's game: it runs the game of $GAME_DIR (a copy of
# the tree, DeckGalaxy) with a settings file, memory card and states of its
# own in $BENCH_HOME. The scenarios are in scenarios.conf.
#
# Environment: GAME_DIR, BENCH_HOME, RG_GS_SCALE (settings.ini's `scale` unless
# set), BENCH_SHOTS=N (a screenshot, PPM, every N frames in
# $BENCH_HOME/runs/LABEL/SCENARIO.n.shots, to see what a scenario does), BENCH_VISIBLE=1 (a visible window, so the compositor is in the
# measure; the default window is hidden: GS, VIF1 and the frame pacing only).
here=$(dirname "$(readlink -f "$0")")
GAME_DIR=${GAME_DIR:-/run/media/deck/SD/Recomp/DeckGalaxy}
BENCH_HOME=${BENCH_HOME:-/run/media/deck/SD/Recomp/RogueBench}
scenario=$1
label=${2:-adhoc}
repeat=${3:-1}
[ -n "$scenario" ] || { sed -n '2,18p' "$0"; exit 2; }
line=$(grep -v '^#' "$here/scenarios.conf" | awk -F'|' -v s="$scenario" '$1 == s')
[ -n "$line" ] || { echo "no scenario '$scenario' in scenarios.conf"; exit 2; }
reads=$(echo "$line" | awk -F'|' '{print $3}')
input=$(echo "$line" | awk -F'|' '{print $4}')
state="$BENCH_HOME/states/$(echo "$line" | awk -F'|' '{print $2}')"
[ -f "$state" ] || { echo "missing state $state"; exit 2; }
[ -x "$GAME_DIR/build/game/src/roguegalaxy" ] || { echo "no game built in $GAME_DIR"; exit 2; }
if pgrep -x roguegalaxy >/dev/null; then echo "a game is running; not starting"; exit 2; fi

export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-gamescope-0}
out="$BENCH_HOME/runs/$label"
mkdir -p "$out"

n=1
while [ "$n" -le "$repeat" ]; do
    work="$BENCH_HOME/work"
    rm -rf "$work"; mkdir -p "$work/states"
    cp "$state" "$work/states/slot1.rgs"
    printf '%s\n' "$(cat "$GAME_DIR/REVISION")" > "$work/states/slot1.rev"
    cp "$BENCH_HOME/settings.ini" "$work/settings.ini"
    [ -n "$RG_GS_SCALE" ] && sed -i "s/^scale = .*/scale = $RG_GS_SCALE/" "$work/settings.ini"
    rm -rf "$work/mc0"; cp -r "$BENCH_HOME/mc0" "$work/mc0"
    # The script ends with one press of Select at the last measured read; the
    # game then quits itself (RG_INPUT_QUIT), so the present log stops there.
    log="$out/$scenario.$n.log"; csv="$out/$scenario.$n.csv"
    hidden=1; [ -n "$BENCH_VISIBLE" ] && hidden=
    [ -n "$BENCH_SHOTS" ] && mkdir -p "$out/$scenario.$n.shots"
    cd "$work" || exit 1
    env RG_SETTINGS_FILE="$work/settings.ini" RG_MC0_DIR="$work/mc0" \
        RG_MUTE=1 ${hidden:+DQ8_GFX_HIDDEN_WINDOW=1} RG_HOME=0 \
        RG_FPS=60 RG_TIME_SCALE=fixed \
        RG_STATE_SCRIPT="load 0@1" \
        RG_INPUT="$input${input:+,}$reads:SELECT:1" RG_INPUT_CLOCK=pad RG_INPUT_FROM_LOAD=1 RG_INPUT_QUIT=1 \
        DQ8_GFX_STATS_EVERY=10 DQ8_GFX_PRESENT_LOG="$csv" \
        ${BENCH_SHOTS:+DQ8_GFX_SCREENSHOT_DIR="$out/$scenario.$n.shots" DQ8_GFX_SCREENSHOT_EVERY=$BENCH_SHOTS} \
        "$GAME_DIR/build/game/src/roguegalaxy" "$GAME_DIR/disc/SCUS_974.90" >"$log" 2>&1 </dev/null &
    pid=$!
    # The game ends with its script; a generous wall-clock limit guards a hang.
    limit=$(( reads / 20 + 180 )); t=0
    while kill -0 $pid 2>/dev/null; do
        [ "$t" -ge "$limit" ] && { echo "timeout: killed after ${limit}s"; kill $pid; break; }
        sleep 1; t=$((t + 1))
    done
    sleep 1; kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null
    cd "$here" || exit 1
    python3 -I "$here/analyze.py" "$log" "$csv" | tee "$out/$scenario.$n.txt"
    n=$((n + 1))
done
