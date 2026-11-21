#pragma once

#include <functional>
#include <cstdint>
#include <string>
#include <vector>

class PS2Runtime;

// Save states: the guest-visible machine (memories, the EE kernel's threads and
// objects, the IOP and SPU2) written to a slot file and read back, in the run
// that saved or another. The GS front end and the host-side caches are not
// part of it. See docs/SAVESTATES.md.
//
// A request runs on the EE executor between guest dispatches. A save that
// finds a callback in flight or a thread parked where it cannot be rebuilt
// asks again at the next pass, for up to five seconds.
//
// RG_STATE_SCRIPT="save 1@30 load 1@60": scripted requests, the seconds counted
// from the call to install(), for tests without a keyboard.
namespace rg::savestate
{

constexpr int kSlots = 5;

// RG_STATE_SCRIPT's slot 0: Continue's load (the slot saved last).
constexpr int kAutoSlot = 0;

// After the runtime is set up: starts RG_STATE_SCRIPT, when set.
void install(PS2Runtime &runtime);

// Asynchronous: the request is queued, the result is told in the log and,
// through takeMessage(), for the menu's toast. Slots are 1..kSlots.
void requestSave(PS2Runtime &runtime, int slot);
void requestLoad(PS2Runtime &runtime, int slot);

// Continue: loads the slot saved last (of this build), once the game has booted.
// Nothing when there is none.
void requestAutoLoad(PS2Runtime &runtime);

// A frame of the game for the slot that is being saved (RGBA, top row first),
// from the presenter's thread, after requestSave: scaled down and kept beside
// the state. Whichever of the two arrives second writes the file.
void offerThumbnail(int slot, const uint8_t *rgba, uint32_t width, uint32_t height);

// Waits (up to ten seconds) for the files still being written, as the game
// ends: a save made just before quitting is otherwise cut short.
void flush();

// The next line to show the player ("Saved to slot 2"), false when none.
bool takeMessage(std::string &text);

// What each slot holds, for the menu: whether there is a state, and when it
// was written ("2026-11-19 00:05"). One entry per slot, in order.
struct SlotInfo
{
    bool used = false;
    std::string when;
    // A picture of the game at that moment (a BMP on disk), empty while there
    // is none.
    std::string picture;
};
std::vector<SlotInfo> slots();

// The slot Continue would load, in the same terms (never a picture).
SlotInfo autoSlot();

} // namespace rg::savestate
