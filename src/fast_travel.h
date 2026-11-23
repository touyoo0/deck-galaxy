#pragma once

#include <string>
#include <vector>

class PS2Runtime;

// Fast travel from the full map (Select in the field).
//
// With the full map open (FieldExploreModeUpdate's phase 0x190, which asks
// PadIsTriggered(Select) every frame at 0x283DDC to close it), the d-pad
// moves a selection over the transporters the player has already activated,
// the same list the transporter menu's Teleport page shows, and Cross
// travels to the one selected. The map is then closed the game's own way
// (the Select answer is turned into a yes) and the warp is queued with the
// requests the transporter menu's Teleport pushes (mode 5 between the two
// warp fades, 0x320 and 0x321) after filling the same warp record at
// 0x4D7E00. Nothing is written to the save.
//
// With the menu's game option `fast_travel` ("Fast travel from the map"), or
// RG_FAST_TRAVEL=1. Off by default.
namespace rg::fasttravel
{

// After loadELF().
void install(PS2Runtime &runtime);

void setEnabled(bool on);
bool enabled();

// What the player is told, once: the selection (`index` of `count`, from 1),
// or `count` 0 when no transporter is activated on the planet. The menu
// words it (and translates it). Called from the frame thread; false when
// none.
struct Toast
{
    int index = 0;
    int count = 0;
};
bool takeToast(Toast &toast);

// The list the menu draws over the map while it is open: the activated
// transporters' names, in order, and the one chosen. Any thread.
struct List
{
    bool open = false;
    int selected = 0;
    std::vector<std::string> names;
};
List list();

} // namespace rg::fasttravel
