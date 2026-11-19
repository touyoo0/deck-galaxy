#pragma once

#include <string>
#include <vector>

class PS2Runtime;

// Quick save and quick load: the guest-visible machine (memories, the EE
// kernel's threads and objects) written to a slot file and read back. Phase 1
// prototype: the IOP (sound), the GS front end and the host-side caches are
// not part of it, so a load is only sound in the process that saved, from a
// calm moment. See docs/SAVESTATES.md.
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

// After the runtime is set up: starts RG_STATE_SCRIPT, when set.
void install(PS2Runtime &runtime);

// Asynchronous: the request is queued, the result is told in the log and,
// through takeMessage(), for the menu's toast. Slots are 1..kSlots.
void requestSave(PS2Runtime &runtime, int slot);
void requestLoad(PS2Runtime &runtime, int slot);

// The next line to show the player ("Saved to slot 2"), false when none.
bool takeMessage(std::string &text);

// What each slot holds, for the menu: whether there is a state, and when it
// was written ("2026-11-19 00:05"). One entry per slot, in order.
struct SlotInfo
{
    bool used = false;
    std::string when;
};
std::vector<SlotInfo> slots();

// The slot the keys act on, 1..kSlots.
int currentSlot();
void setCurrentSlot(int slot);

} // namespace rg::savestate
