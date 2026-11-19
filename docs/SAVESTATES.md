# Save states (quick save and quick load)

**In short:** five slots that hold the whole game at one moment, as an
emulator's save states do, written to a file and read back at once, in the
same run or after the game has been closed. They sit beside the game's own
saves; they do not replace them.

## Using them

| | Keyboard | Controller (Steam Deck) |
|---|---|---|
| Quick save | F2 | Back + R1 |
| Quick load | F3 | Back + L1 |
| Previous / next slot | Page Up / Page Down | Back + D-pad left / right |

The pause moved from F2 to F5 to make room (and the menu's frame-rate
counter, F3, is gone: the Steam Deck has its own). On a Steam Deck, Steam Input can give a back button a key, which
puts them under a finger: for example L4 = F1 (the menu), R4 = F2 (save) and
R5 = F3 (load).

The F1 menu's **Save states** menu lists the five slots with a picture of
the game and the time of each, and saves into, loads from or picks any of them.
A message says what happened. The files are in `states/` next to
`settings.ini` (`~/.local/share/RogueGalaxy/states/slot1.rgs` to `slot5.rgs`,
and `slotN.<time>.thumb.bmp`, the picture, 69 KB). A state is 42 MB in memory
and about 10 MB on disk: zstd-compressed (0.05 s to compress, 0.04 s to
decompress), written by a thread of its own so the game does not stop for it,
and waited for when the game quits.

A state is refused, with a message, **while a movie plays** (the cutscenes and
the title screen: the video decoder is not part of it), **while a callback, a DMA
transfer or a wait that cannot be rebuilt is in progress** (it tries again for
five seconds),
and when it was saved **in another area of the game** than the one running now
(the game's code changes with the area: load it from there).

## How it works

`patches/ps2recomp/0040-save-state-machine.patch` adds
`EeScheduler::saveMachine` and `loadMachine`, which write and read:

- the guest's memories: RAM, scratchpad, VU code and data, GS local memory,
  the I/O, VIF, DMA and GS registers, the timers;
- the EE kernel: threads, semaphores, event flags, alarms, interrupt handlers,
  the deadlines (as time left), the guest heap's bookkeeping;
- the emulated IOP and SPU2, which is the sound: the IOP's memory, threads and
  kernel objects, the SPU2's memory, voices and effects.

The part that looked hardest was the guest's threads, which run on host
stacks. A thread parked inside a syscall (every wait) needs nothing of its
stack: the generated code stores the pc after the syscall before calling the
handler, and every function starts with a switch on `ctx->pc` that jumps to the
label after each call or syscall, so a load rebuilds the thread from its
registers and dispatches that pc, as for a thread a checkpoint unwound. The
handlers that park set their result first and do nothing after.

`src/savestate.cpp` runs a request on the EE executor, between guest
dispatches (`EeScheduler::requestExecutorTask`), after waiting for the VIF1
thread's lists. The stream starts with a magic, a format version, the RAM size
and the resident overlay's name; the file wraps it in a small header (magic,
codec, size). `libzstd.so.1` is loaded when first needed (`dlopen`), not
linked: the build container has no development files for it. Without it a
state is written uncompressed, and a compressed one cannot be read. A state
written by the first build (no wrapper) still loads.

The thumbnail is a frame the presenter captures when the save is asked
(`backend.requestCapture`), scaled to 192 pixels wide and written as a BMP
(`dq8-gfx` reads BMP since this feature). Its name carries a time because the
menu keeps a file's pixels as long as the process runs.

## What is not in a state

The GS front end's registers and the GPU's caches (the game redraws them),
the video decoder, the memory card's and the disc's open handles, and the
achievements' progress. Loading while the game reads the disc or the card is
the risk that is left. `RG_STATE_SCRIPT` and `RG_INPUT` make a test without a
keyboard (see [DEVELOPMENT.md](DEVELOPMENT.md)).
