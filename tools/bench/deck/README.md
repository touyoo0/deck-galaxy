# Performance bench on the Steam Deck

A run that plays the same scene the same way every time, unattended, and
measures its frame times, so a change to the code can be compared with the
one before it.

```sh
tools/bench/deck/run.sh battle1 base 3        # 3 runs of a scenario, label "base"
# ... change the code, rebuild (cmake --build build/game in the container) ...
tools/bench/deck/run.sh battle1 mychange 3
python3 tools/bench/deck/compare.py base mychange
```

Results are in `$BENCH_HOME/runs/LABEL/` (`SCENARIO.N.log`, `.csv` the present
log, `.txt` the summary, `.json` for `compare.py`).

## Why it replays alike

- **The start is a save state** (`RG_STATE_SCRIPT="load 0@1"`), taken in the
  scene to measure (a battle, a street): the whole machine, so the scene
  starts identical, with no menu to cross.
- **The input counts the game's pad reads from the load**
  (`RG_INPUT_CLOCK=pad`, `RG_INPUT_FROM_LOAD=1`), not seconds: a slower host
  gets the same press at the same frame.
- **The game's steps are fixed** (`RG_TIME_SCALE=fixed`): the logic does the
  same half steps whatever the host's speed, so a slow frame does not change
  what the scene does next.
- **It ends with its script** (`RG_INPUT_QUIT=1`): the present log covers
  exactly the measured pad reads.
- Own settings file, memory card and states folder in `$BENCH_HOME` (a copy
  made fresh for each run): the player's saves are never touched, and the
  settings (`scale`, `present`...) are the same for every run until you change
  `settings.ini` there or set `RG_GS_SCALE`.

`analyze.py` prints a fingerprint of the scene (the meshes drawn each stats
interval). Battles replay exactly (same fingerprint every run). A walk in the
field can still drift in one stretch (a run in three left the others' meshes by
up to 11% for a while, then came back: something in the field still follows the
host's timing, not found yet), so `compare.py` calls scenes "alike" when 90% of
the intervals agree within 2%, and says "not comparable" below that. Run each
side three times: a difference smaller than the runs' own spread is noise.
`BENCH_SHOTS` (screenshots) changes the scene's cost and its fingerprint: use
it to look at a scenario, never in a run that is measured.

## Scenarios (`scenarios.conf`)

One line each: name, the state in `$BENCH_HOME/states/` (a quick save copied
from `~/.local/share/RogueGalaxy/states/slotN.rgs`; take a new one in the game
with F2 and copy it, never point the bench at the player's folder), how many
pad reads to measure, and the pad input (`RG_INPUT`'s syntax, reads counted
from the load).

`field1.rgs` (the forest, no fight) is made by `make-field-state.sh`, which
navigates the title menu by scripted input and saves a state in the field;
the other states are quick saves of the player's.

## What the numbers are

`fps` is frames over the time between the first and the last present of the
window (after 120 frames of warm-up). `1% low` is the frame rate of the
slowest 1% of the frames; `p99` the frame time they start at; `> 25 ms` the
count of frames that miss a 40 fps pace. The GS backend, VIF1 and GIF shares
say which thread limits.

The window is visible by default, as in play (gamescope composes it, the
settings' `present` mode counts); `BENCH_HIDDEN=1` hides it (the GS, the VU1 and
the pacing only; about a frame per second better). `RG_GS_SCALE=3` is the
stress profile: with the settings' scale 2 the scenarios hold 56-60 fps, which
leaves little to improve; at 3 `field_idle` falls to 47-51 fps (single runs,
2026-11-24), a drop a change can be measured against.

`BENCH_UNPACED=1` runs without the 60 fps cap (`RG_SPEED=0`): the frame rate is
then what the machine can do, so a change shows even where the paced run holds
60. The scene stays the same (the steps are fixed and the input counts pad
reads). 2026-11-24, scale 2, hidden window: `battle2` 80 fps (45.6 for the 1%
low), `field_idle` 57 fps: it never reaches 60, VIF1 at 92%.

`BENCH_PERF=1` records the run with `perf` (`SCENARIO.n.perf`; `BENCH_PERF_ARGS="--call-graph dwarf,16384"` for
call chains, a 650 MB file for 35 s: delete it after). `perf report -i FILE --no-children --sort comm,sym` splits
the time by thread (GameThread = the EE and the game, VIF1, the main thread); a profiled run is slower (54 fps
against 57.8), so read shares, not fps. `field_idle`, 2026-11-24: GameThread 46%, VIF1 38%, main 16% of the
samples; of VIF1's own time `runVu1` 66% (`buildMesh` 15%, `readChunk` 10%), `deliverPacket` 39% (`mesh` 16%,
`native2d::build` 16%, `unpackPlain` 11%) and `operator new` 11%, a flat profile with no single hot spot.

**Compare two builds with `ab.sh`, not with two labels measured an hour apart**: the machine drifts (the
same binary gave 57.8 fps and, forty minutes later, 56.6: heat or background load), more than most changes
gain. `ab.sh SCENARIO BASE NEW 3` alternates the two binaries (copies kept in `$BENCH_HOME/bin/`), and
`compare.py` prints the paired deltas: a change is real when the new side is ahead in every pair.

## Optimisations tried (field_idle, unpaced, alternated with the build before, 3 pairs)

| Change | Result |
|---|---|
| `native2d::build` keeps its A+D packets' storage from one call to the next (as it did for the meshes' vertices) | kept: +1.11, +0.91, +0.54 fps (mean +0.85, 3 of 3 pairs) |
| `unpackPlain` (PS2Recomp, patch 0043): the 32-bit shapes with the component count fixed at compile time, V4 in one copy | dropped: -0.57, -0.24, -0.82 fps (0 of 3). Its time is the memory (cold stream data), not the call into `memcpy` |
