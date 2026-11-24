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
interval). Two runs of a scenario with different fingerprints played different
scenes: `compare.py` says so and refuses to compare them. Run each side
several times: a difference smaller than the runs' own spread is noise.

## Scenarios (`scenarios.conf`)

One line each: name, the state in `$BENCH_HOME/states/` (a quick save copied
from `~/.local/share/RogueGalaxy/states/slotN.rgs`; take a new one in the game
with F2 and copy it, never point the bench at the player's folder), how many
pad reads to measure, and the pad input (`RG_INPUT`'s syntax, reads counted
from the load).

## What the numbers are

`fps` is frames over the time between the first and the last present of the
window (after 120 frames of warm-up). `1% low` is the frame rate of the
slowest 1% of the frames; `p99` the frame time they start at; `> 25 ms` the
count of frames that miss a 40 fps pace. The GS backend, VIF1 and GIF shares
say which thread limits.

The window is hidden by default (`DQ8_GFX_HIDDEN_WINDOW`): the GS, the VU1 and
the pacing are measured, the compositor is not. `BENCH_VISIBLE=1` shows it.
