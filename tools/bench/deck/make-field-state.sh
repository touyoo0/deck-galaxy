#!/bin/sh
# Makes the "field" state without touching the keyboard: boots the game on a copy of
# the bench's memory card, goes through the (French) title menu: Continuer, the
# memory card's slot 1, the first save, and its confirmations, and saves a state
# once the party stands in the field (about 145 s after the start).
#
#   tools/bench/deck/make-field-state.sh [OUTPUT=$BENCH_HOME/states/field1.rgs]
#
# The pad reads below were found by looking at screenshots (BENCH_SHOTS-style
# DQ8_GFX_SCREENSHOT_DIR runs); another language or save changes them.
GAME_DIR=${GAME_DIR:-/run/media/deck/SD/Recomp/DeckGalaxy}
BENCH_HOME=${BENCH_HOME:-/run/media/deck/SD/Recomp/RogueBench}
out=${1:-$BENCH_HOME/states/field1.rgs}
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-gamescope-0}
work="$BENCH_HOME/mk"; mkdir -p "$work/states"; rm -f "$work/states/slot1.rgs"
rm -rf "$work/mc0"; cp -r "$BENCH_HOME/mc0" "$work/mc0"; cp "$BENCH_HOME/settings.ini" "$work/settings.ini"
input="909:CROSS:5,999:CROSS:5,1059:DOWN:5,1118:CROSS:5,1238:CROSS:5,1358:CROSS:5,1478:CROSS:5,1569:CROSS:5,1629:CROSS:5,2019:CROSS:5,2069:CROSS:5,2400:LEFT:5,2460:CROSS:5,2540:UP:5,2600:CROSS:5,2800:CROSS:5,3000:CROSS:5,3300:CROSS:5,3700:CROSS:5"
cd "$work" || exit 1
env RG_SETTINGS_FILE="$work/settings.ini" RG_MC0_DIR="$work/mc0" RG_MUTE=1 DQ8_GFX_HIDDEN_WINDOW=1 RG_HOME=0 \
    RG_FPS=60 RG_TIME_SCALE=fixed RG_INPUT="$input" RG_INPUT_CLOCK=pad RG_STATE_SCRIPT="save 1@145" \
    timeout 165 "$GAME_DIR/build/game/src/roguegalaxy" "$GAME_DIR/disc/SCUS_974.90" >"$work/log.txt" 2>&1
[ -f "$work/states/slot1.rgs" ] && cp "$work/states/slot1.rgs" "$out" && echo "state written to $out" || { echo "no state saved: see $work/log.txt"; exit 1; }
