#pragma once

namespace rg::debug
{

// RG_INPUT="SECONDS:BUTTON[+BUTTON...]:MS[,...]" holds those pad buttons for
// MS milliseconds, SECONDS after start, in place of the real pad: a scripted
// player for unattended test runs, e.g. RG_INPUT="30:START:150,33:CROSS:150".
// Buttons: SELECT L3 R3 START UP RIGHT DOWN LEFT L2 R2 L1 R1 TRIANGLE CIRCLE
// CROSS SQUARE, and the sticks pushed fully: LUP LDOWN LLEFT LRIGHT, RUP RDOWN
// RLEFT RRIGHT.
// With RG_INPUT_CLOCK=pad, SECONDS and MS count the game's pad reads (one a
// frame) instead, so a script replays alike on a slower host (RG_GS=sw, the
// VU interpreter); each [input] line logs the read count to convert with.
// RG_INPUT_QUIT=1 quits the game when the script is over.
// RG_INPUT_FROM_LOAD=1 (with the pad clock) counts the script from the pad read a
// loaded save state (RG_STATE_SCRIPT) continues from: a scene replayed from a
// state then gets the same input at the same frame whatever the boot took.
void startInputScript();

} // namespace rg::debug
