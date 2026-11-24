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

## Power and temperature (`sample.py`, run by `run.sh`)

Each run samples the APU's power (amdgpu `power1_average`), its temperatures, the GPU's busy share and clock, the
CPU clock and the CPU time of each thread, and `analyze.py` prints them for the measured window (the screen and the
battery are not in the APU figure). `compare.py` adds watts and Tctl. 2026-11-24, hidden window, paced at 60:

| What | `battle2` | `field_idle` |
|---|---|---|
| scale 2 (the settings), GPU forced at 1600 MHz by the system | 10.2-11.2 W, 59.5 fps | 14.6-14.8 W, 57.3 fps |
| scale 1 / scale 3 | 8.8 W / 12.8 W (59.4 / 59.3 fps) | |
| GPU clock 1000 MHz instead of 1600 (steamos-manager `ManualGpuClock`) | 8.9 W, 59.5 fps | 13.7 W, 56.6 fps |
| GPU clock 700 MHz | 9.4 W (GPU 37% busy) | 13.8 W, 56.1 fps |
| CPU scheduler `lavd` (steamos-manager `CpuScheduler`) instead of `none` | 60.0 fps, p95 18.2 against 19.6 ms, same watts | same |
| whole process on one hardware thread a core (`taskset -c 0,2,4,6`), unpaced | | 58.3 against 57.0 fps (+1.08, 3 of 3 pairs) |
| the three busy threads each pinned to their own core | | -0.3 fps (no gain) |

The game uses about 2 cores (GameThread 0.8, VIF1 0.75-0.85, main 0.4): a flat profile, no thread idles, so
power follows the work; the system keeps the GPU at its top clock (`ManualGpuClock` 1600) at 15-30% load.

## Why a cache of the VU1 chunks does not pay (2026-11-24, `field_idle`, ~3000 meshes a frame)

The idea: the forest's scenery is the same every frame, so build each chunk once. The profile says what it
could save: `buildMesh` (with `readChunk`) is 14.8% of the VIF1 thread, about 2.6 ms a frame, 0.9 us a chunk.
To know a chunk is unchanged its inputs have to be hashed (its section of VU data memory and the quads the
program reads, 1.5-3 KiB: 0.2-0.4 us), so even every chunk hitting would save 0.5 us of the 0.9, 1.5 ms a frame
(+4 fps at the very most); the characters move and miss. And `nativeproducer::mesh` (16%) and
`native2d::build` (16%) cannot be cached that way: they read and change the draw state, the targets and the
counters. What would move the VIF1 limit is running those two on another thread (35-40% of VIF1's time), which
the effects, the composites and the draw-state mirror in `src/vif1_thread.cpp` make a redesign, not a patch.

## The producer thread (RG_PRODUCER_THREAD, 2026-11-24)

`nativeproducer::mesh` and `piece` (a third of the VIF1 thread's time) run on the GIF stage's thread, in the
stage's order, so the VIF1 thread goes on to the next chunk. `field_idle` unpaced, alternated with
`RG_PRODUCER_THREAD=0`, 3 pairs: +5.39, +4.22, +4.45 fps (56.7 to 61.4; 1% low 37.3 to 40.0, frames over 25 ms
21 to 7), watts and Tctl unchanged within noise. `battle2` does not move (79 fps either way: the game thread
is its limit). What it took to be right, found with `frames.py`: the producer calls the VIF1 thread makes through
the `producer::` alias (`takeProduced`, `noteWithheld`) change what the meshes read and wait for the stage like
the uploads and effects do; and the game thread drains the stage at each flip before the Present record.

Checking it: two runs of one build give identical frames only while the guest does the same thing (battle2 30 of
30), and the game's logic follows the timing of the host (a field walk drifted in one run of three before this
change): a run with the producer thread diverges from one without within a few hundred frames (an animation phase,
the outcome of a fight), so frames differ for that reason, not for the renderer's. Where the guest agrees,
the frames are identical to the pixel (`field_idle` flips 300, 900, 1500: 0.00% of the pixels). Not proven: a
long play with every kind of scene.
