#!/bin/bash
# A scripted, hidden-window run of a Deck game folder from a quick save, for
# comparing two builds or a switch on and off on the same fight.
#
#   tools/bench/battle_ab.sh NAME [SECONDS [SPEED]]   one run, log in $WORK/NAME.log
#   tools/bench/battle_ab.sh --alternate A=VAR=0 B=VAR=1 [...]  alternate runs
#
# GAME   the game folder to run (default: the current one; never the player's)
# WORK   the folder for the settings, states and logs (default ~/rg-bench);
#        put a copy of settings.ini and states/slotN.rgs (+ .rev) there: the
#        run loads "slot 0" (the last saved) at 1 s, as Continue does
# MC0    the memory card copy to use (default $WORK/mc0)
# SETTING  extra environment for the run, e.g. "RG_MESH_STAGE=0"
# SPEED  0 runs unpaced (RG_SPEED=0), which shows what the game could do
#        beyond the 60 fps cap; a heavy fight capped at 60 shows nothing
#
# From SSH, start it with systemd-run --user -E DISPLAY=:1 -E XDG_RUNTIME_DIR=/run/user/$UID
# (a process started over SSH dies with the session). Read the logs with
# tools/bench/battle_summary.py.
GAME=${GAME:-$PWD}
WORK=${WORK:-$HOME/rg-bench}
NAME=$1; SECS=${2:-70}; SPEED=$3
[ -n "$NAME" ] || { sed -n '2,19p' "$0"; exit 1; }
mkdir -p "$WORK"
cd "$GAME" || exit 1
export RG_SETTINGS_FILE=$WORK/settings.ini RG_MC0_DIR=${MC0:-$WORK/mc0}
export RG_MUTE=1 DQ8_GFX_HIDDEN_WINDOW=1 DQ8_GFX_STATS_EVERY=60
export RG_HOME=0 RG_SKIP_OPENING=1 RG_STATE_SCRIPT="load 0@1"
[ -n "$SPEED" ] && export RG_SPEED=$SPEED
[ -n "$SETTING" ] && export "$SETTING"
exec timeout -s KILL "$SECS" "$GAME/build/game/src/roguegalaxy" disc/SCUS_974.90 >"$WORK/$NAME.log" 2>&1 </dev/null
