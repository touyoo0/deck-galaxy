# Save states (quick save and quick load)

**In short:** five slots that hold the whole game at one moment, as an
emulator's save states do, written to a file and read back at once, in the
same run or after the game has been closed. They sit beside the game's own
saves; they do not replace them.

## Using them

| | Keyboard |
|---|---|
| Save | F2 |
| Load | F3 |

On a controller there is no button combination: open the F1 menu (View +
Menu on a Steam Deck), then **Save states**: the page lists the five slots to save into and the five to load from, side by side.

Each opens a window with the five slots, each with a picture of the game and
the time it was saved (or "empty"): **nothing is saved or loaded until you
pick a slot**. The game waits, paused, behind it. Up and down move the cursor,
Enter or A picks, Esc or B leaves; the mouse works too. Saving over a slot
that holds a state asks first; an empty slot cannot be loaded. The F1 menu's
**Save states** page does the same without F2 and F3.

The pause moved from F2 to F5 to make room (and the menu's frame-rate
counter, F3, is gone: the Steam Deck has its own). On a Steam Deck, the
installer gives the back buttons a key when it adds the game to Steam (Steam
Input, a layout of the Gamepad With Joystick Trackpad template): L4 = F1 (the
menu), L5 = F5 (pause), R4 = F2 (save) and R5 = F3 (load). A layout the game
already has in Steam is not touched; Steam shows the new one after a restart
(game mode does it).

The files are in `states/` next to `settings.ini`
(`~/.local/share/RogueGalaxy/states/slot1.rgs` to `slot5.rgs`, and
`slotN.<time>.thumb.bmp`, the picture, 69 KB). A state is 42 MB in memory and
about 10 MB on disk: zstd-compressed (0.05 s to compress, 0.04 s to
decompress), written by a thread of its own so the game does not stop for it,
and waited for when the game quits.

## Continue

The home screen offers **Continue**, above Play, when a slot holds a state:
it starts the game and loads the slot saved last (there is no state of the
game's own any more: it was taken when the window left the front and at quit,
and what it carried went wrong more often than a slot does). Play starts from
the title as before. Quitting does not save: what was done since the last
quick save is lost to Continue. A slot is only offered by the build that took
it (`slotN.rev` beside it): after an update Continue is gone until the next
quick save, so a state is never run under other code (loading a slot by hand
still loads it). The game's own saves (the memory card) are untouched by all
this.

Continue does not load at once: the guest has not run when the home screen
is shown, and the host's side of the boot (the sound driver and its audio
stream, the RPC tables, the game's pad ports) is not in a state, so the game
boots first and the state loads when it has run 1.5 s and opened its pad (and
asks again while a logo movie plays, up to 20 s).

A state is refused, with a message, **while a movie plays** (the cutscenes and
the title screen: the video decoder is not part of it), and **while a callback, a
DMA transfer or a wait that cannot be rebuilt is in progress** (it tries again
for five seconds). A state loads from any area of the game: the code that goes
with the area it was saved in is made the running one first.

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

The pad ports follow the machine as a trailer (which are open, analog or
digital, pressure, the button mask): the runtime keeps them outside the machine,
and a game that had not opened a port yet (Continue, from the home screen)
would read "controller not connected". A state without the trailer loads as
before.

The disc and the renderer follow the machine as trailers too. The disc
(`patches/ps2recomp/0041-cd-state.patch`): the files the game has looked up
with the sector number it holds for each (a new process would number them in
another order), the stream and its pace. The native renderer's side
(`nativeproducer::saveState`, `drawstate::saveState`): its textures, palettes
and targets are made from what the game uploads, and the GS never sees those
uploads, so GS memory in the state has none; the tables from the game's
addresses to the renderer's records go in the state with their texels, and a
load sends the records again. The small offscreen targets the game draws
once and samples for a while (a cached shadow, a minimap: up to 512 by 512,
about 5 MB) are read back from the renderer and drawn into the targets again
(`saveFills`, `TargetFill`): the game does not draw them after a load, and
the draws that sample them were dropped or sampled garbage. A state from
before has no such trailer.

## What is not in a state

The contents of the renderer's big offscreen targets (the frame's own, which
the game draws again at once), the
video decoder, the memory card's and the disc's open handles, and the
achievements' progress. Loading while the game reads the disc or the card is
the risk that is left. `RG_STATE_SCRIPT` and `RG_INPUT` make a test without a
keyboard (see [DEVELOPMENT.md](DEVELOPMENT.md)).
