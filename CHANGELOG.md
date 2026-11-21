# Changelog

## Unreleased

- The home screen makes soft sounds: a light tick when the selection moves, a tap when a button is pressed, and a warm chord when Play starts the game.
- A new game starts with the camera rotation on "Reversed"; existing saves keep their own choice.
- No more button combinations: L4 (F1) opens the menu. Developer tools removed.
- Native renderer, lighter on the Deck: steady 60 fps, about 4 W less. Starts at 2x. Texture packs are not drawn for now.
- No "Fullscreen" option or F11: the game is always fullscreen on the Deck.
- Quick loads keep the music, the battles and the textures, also after restarting the game.
- Continue loads your last quick save, not the place where you left the game.

## 0.11.0 - 2026-11-19

- Continue: the home screen resumes where you left the game.
- Save states: 5 slots, F2 to save, F3 to load (see `docs/SAVESTATES.md`).
- Back buttons set when adding to Steam: L4 menu, L5 pause, R4 save, R5 load.
- Illustrated tips in the installer while the game builds.
- Loading transition after Play.
- Redesigned home screen and settings; the update now sits in "What's new".
- The menu is one centered window, with new Save states, Actions and Help pages.
- Pause is on F5 (was F2).
- The game pauses when its window loses focus (new default).

## 0.10.0 - 2026-11-18

- Installer, home screen and settings in English, French, German, Spanish and
  Italian.

## 0.9.0 - 2026-11-18

- Options button on the home screen.
- Removed the options that mean nothing on the Deck.

## 0.8.0 - 2026-11-18

- The picture is always 16:10, the Deck's screen.

## 0.7.0 - 2026-11-18

- RetroAchievements: the game's 130 achievements are tracked as you play.

## 0.6.0 - 2026-11-18

- The home screen shows "What's new", readable with the controller.

## 0.5.0 - 2026-11-18

- The installer comes as an AppImage: download it and double-click.
- New installer look and a new home screen.
- Adding to Steam also sets the game's pictures.

## 0.4.0 - 2026-11-17

- Installer window: disc image, folder, then it builds the game and adds it to
  Steam.
- Home screen (Play, Update, Quit) with in-game updates.
- First install is faster, and frames reach the screen at an even pace.

## Before 0.4.0

Versions 0.1.0 to 0.3.0 were Windows releases of the upstream project
([victor-takai/rogue-galaxy-recomp](https://github.com/victor-takai/rogue-galaxy-recomp)).
Their notes are in its history, not here.
