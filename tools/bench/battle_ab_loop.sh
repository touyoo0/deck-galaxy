#!/bin/bash
# Alternates runs of battle_ab.sh: tools/bench/battle_ab_loop.sh "off:RG_MESH_STAGE=0" "on:RG_MESH_STAGE=1" ... 
# Each argument is NAME:SETTING; the list runs ROUNDS times (default 3), logs
# NAME1.log... in $WORK. SPEED=0 for unpaced runs. See battle_ab.sh.
HERE=$(dirname "$(readlink -f "$0")")
WORK=${WORK:-$HOME/rg-bench}
for round in $(seq 1 "${ROUNDS:-3}"); do
  for spec in "$@"; do
    name=${spec%%:*}; setting=${spec#*:}
    SETTING=$setting "$HERE/battle_ab.sh" "${name}${round}" "${SECS:-70}" "$SPEED"
    sleep 5
  done
done
touch "$WORK/ab.done"
