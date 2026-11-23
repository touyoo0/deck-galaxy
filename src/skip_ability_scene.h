#pragma once

class PS2Runtime;

// The scene of an ability (a spell, a special move: game mode 10, run by
// AbilityModeRun, 0x2790E0) skipped as soon as the game allows it, with no
// button pressed. The game lets a press of its skip button end the scene once
// the scene's files have loaded (the check reads PadIsTriggered, 0x1839F0,
// from 0x279698, which returns to 0x2796A0); with the menu's game option
// `skip_ability_scene`, or RG_SKIP_ABILITY_SCENE=1, that one read answers
// "pressed". Every other read of the pad is left alone, and the scene ends as
// it does when the player skips it, so what the ability does is unchanged.
namespace rg::skipabilityscene
{

// After loadELF().
void install(PS2Runtime &runtime);

void setEnabled(bool on);

// How far along the transition over a scene left out is, 0 to 1, for the
// host's overlay to draw (a flash with streaks of light and dark corners that
// open again, so the game does not jump from the cast to the fight); below 0
// when there is none.
float burstProgress();

} // namespace rg::skipabilityscene
