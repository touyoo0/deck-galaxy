# Changelog

## Unreleased

### Added

- The installer's progress page shows illustrated tips while the game builds
  (why the wait, the menu, the home screen, save states, the back buttons, 60
  fps, the options, achievements, the languages): a drawing, a title and a few
  lines, in the window's language, one every 9 seconds. `--demo` shows that
  page with a made-up build, to look at it without installing anything.
- A loading transition after Play: the home screen fades out as the picture
  dives into a stream of stars, then a loading screen (logo, bar) stays up
  until the game is drawing and lifts in a jump forward.
- Save states: five slots that hold the whole game at one moment, loaded at
  once, in the same run or after closing the game. F2 opens the slots to save
  into and F3 the slots to load from; nothing is saved or loaded until you pick
  a slot, each with a picture of the game and the time, and a used slot asks
  before it is overwritten. The menu's new Save states has the same two
  entries, for a controller. A state takes about 10 MB on disk. Not while a
  movie plays. See `docs/SAVESTATES.md`.
- Adding the game to Steam also sets the Steam Deck's back buttons: L4 opens
  the menu (F1), L5 pauses (F5), R4 saves a state (F2) and R5 loads one (F3).
  A layout you already made for the game is left alone.

### Changed

- The hints for the Japanese and European discs and for updating named
  `Install-Undub.bat`, `Install-European.bat` and `Update.bat`, which belong
  to the Windows version. They now say to add the disc in the installer, and
  to use Update on the home screen.
- The help line under an option in the menu runs across the whole width of the
  window instead of a narrow column beside the name.
- The menu has no bar along the top any more: everything is in the window in
  the middle of the screen, a new Save states page (the five slots to save into and the five to load from, side by side), an Actions page (pause, fast-forward, mute,
  screenshots, fullscreen, renderer statistics) and a Help page
  (shortcuts, project page, report a problem, About) joining the list of pages,
  all reachable with the controller. F1 opens that window, and Y no longer
  goes to a bar.
- The pause is on F5 now (it was F2), to make room for the save states'
  keys.
- The home screen and the settings are redrawn: flat dark panels with a
  hairline edge, a white pill under the selected entry, thin sliders, soft
  switches and one amber accent, in place of the double white frame, the gold
  plates and the triangle cursor. The home screen drops its moving sparks and
  streaks and keeps the picture, the logo and the notes.
- The home screen's update is no longer a button among Play, Options and Quit:
  it sits at the top of the "What's new" panel, a card with the new version,
  the progress and the errors, a quiet line when there is nothing to do, and a
  gold dot beside the title while an update waits. On a controller, Right
  opens the panel and A starts the update.

### Removed

- The profiler (`RG_PROFILE`) and the hardware watch (`RG_HWWATCH`): they
  worked on Windows only and did nothing on the Deck.
- The menu's frame-rate counter (View > Frame rate, F3, and its setting): the
  Steam Deck has its own.

## 0.10.0 - 2026-11-18

### Added

- The installer, the home screen, the settings, the quit box and the update
  messages are in English, French, German, Spanish and Italian. The language
  is Steam's (else the system's) until you pick one: top right of the
  installer, or Settings > Interface > Menu language in the game. The release
  notes of the last three versions are translated too (`docs/changelog/`).

## 0.9.0 - 2026-11-18

### Changed

- The home screen's release notes show dates as "18 nov. 2026".

### Added

- An Options button on the home screen, above Quit, opens the settings.

### Removed

- Options that mean nothing on the Deck: window mode, vertical sync,
  internal resolutions above 2x, Nearest filter, Borders, Integer scaling,
  Mute and Mute in the background, the Keyboard switch, the controller
  picker, keyboard bindings, the mouse camera's three options, Buttons in
  the game's text (always on), Native renderer, the Textures page, Hide the
  idle mouse cursor (always on) and the frame-rate counter's position (top
  right).

## 0.8.0 - 2026-11-18

### Changed

- The picture is always 16:10, the Deck's screen. The Aspect ratio and
  Widescreen options are gone: the game is drawn wide, and the 4:3 menus and
  screens before the game are stretched to 16:10 instead of barred.

## 0.7.0 - 2026-11-18

### New

- RetroAchievements: the game's 130 achievements are tracked as you play,
  with a notice when one is earned. Sign in once, in the installer or in the
  game (F1 > Settings > Achievements), where all 130 are listed. Softcore
  only; nothing but the unlocks is sent.

## 0.6.2 - 2026-11-18

### Changed

- While the home screen's notes are being read, the buttons show no
  selection (the notes' border turns gold).

## 0.6.1 - 2026-11-18

### New

- The home screen's notes can be read with the controller: Right goes into
  them, Up and Down scroll (L1 and R1 a page), Left or B goes back.

## 0.6.0 - 2026-11-18

### New

- The home screen shows the notes of the last versions ("What's new") on the
  right; the mouse wheel or the right stick scrolls them.

## 0.5.0 - 2026-11-18

### New

- The installer comes as an AppImage (`RogueGalaxy-Setup-x86_64.AppImage`):
  download it and double-click.
- The installer has the game's look: artwork background, one page for the
  disc, the folder and the options. Closing it mid-build asks first, then
  stops the build and deletes the folder it created. The Deck stays awake
  while it installs. A picture in `~/.local/share/RogueGalaxy/theme/`
  replaces the background.
- Adding the game to Steam also sets its pictures (capsule, portrait, banner,
  logo, icon); ones you already set are kept.
- A new home screen: animated artwork, the game's logo, and large buttons
  (Play, Update, Quit) with a spinner while it checks or updates.

## 0.4.0 - 2026-11-17

### New

- `installer/RogueGalaxy-Setup.py`: a window to run once in desktop mode. It
  asks for the disc image and the folder, builds the game and adds it to
  Steam.
- The game opens on a home screen (Play, Update, Quit) and updates itself
  from there, with a progress bar.
- Linux and the Steam Deck (untested): `install.sh` builds the game for
  Linux from your disc image, and `rogue-galaxy.sh` starts it. In a
  launcher such as QuiverLauncher, Play builds the game the first time, in
  a terminal window of its own.

### Changed

- In a launcher such as QuiverLauncher, Play installs the game the first
  time, in a window of its own.
- A first install compiles the game in about a fifth less time.
- A first install configures the game in about a minute and a half less.
- Frames reach the screen at an even pace.

## Before 0.4.0

Versions 0.1.0 to 0.3.0 were Windows releases of the upstream project
([victor-takai/rogue-galaxy-recomp](https://github.com/victor-takai/rogue-galaxy-recomp)).
Their notes are in its history, not here: this fork is for the Steam Deck only.
