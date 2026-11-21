#pragma once

// The home screen's soft sounds (Steam Deck fork: added): a light tick when the
// selection moves, a tap when a button is pressed, a rising chime when Play
// starts the game. They are synthesized here (no sound file), played on a
// stream of their own beside the game's audio, and follow the menu's volume.
namespace dq8::ui::sound {

enum class Cue { Move, Select, Launch };

// 0 to 1, as the menu's volume (0 when muted). RG_MUTE=1 keeps it silent.
void setVolume(float volume);
void play(Cue cue);
void shutdown();

} // namespace dq8::ui::sound
