#pragma once

class PS2Runtime;

// The loading screen of a game being started or loaded: the story so far,
// its lines fading in one by one, then "Press X".
//
// A guest thread of its own draws it (LoadingScreenThread, 0x343510:
// LoadingScreenFrame, then GfxEndFrame's flip) while the main thread loads,
// between LoadingScreenStart and LoadingScreenStop (from GameSessionInit).
// Its fades and its timer count frames, so in the 60 fps mode, where every
// flip waits one vblank, it ran at twice its speed; while it is up the flip
// waits two again (rg::timing::beforeFlip), as for the movies.
//
// "Press X" is drawn once every line is shown, and the game loads in a few
// seconds, well before the lines' slow fade-in is done: as soon as the game
// has loaded (the screen's state 5), the lines left are shown at once, so
// the button comes up with the loaded game.
//
// The instant recap (the menu's game option `instant_loading_recap`, or
// RG_INSTANT_RECAP=1) shows every line at once from the start.
namespace rg::loading
{

// After loadELF().
void install(PS2Runtime &runtime);

// The loading screen is up.
bool active();

void setInstantRecap(bool on);

} // namespace rg::loading
