# SpeedBreaker: Carbon for Steam Deck, Steam Machine and Linux PCs

An unofficial static recompilation of the Xbox 360 version of Need for Speed:
Carbon (the USA Collector's Edition): a native port, not an emulator. A fan
project by project(u), not affiliated with, endorsed by or sponsored by
Electronic Arts or Valve.

This folder has the launcher, `speedbreaker.sh`, and in `lib/` the game
program and the libraries it needs. **Always start the game with
`speedbreaker.sh`** (your Steam shortcut does). It has **no game data**: you
need your own disc image of your own Xbox 360 disc of the Collector's Edition
(USA), an `.iso`; the game installs itself from it on the first run. `VERSION`
says which build this is. It is separate from SpeedBreaker (Most Wanted): its
own folder, its own saves and settings, and either can be installed without
the other.

## 1. Unpack

In a terminal (Konsole), with the download in `~/Downloads`:

    mkdir -p ~/Games && tar -xf ~/Downloads/<the download>.tar.xz -C ~/Games

You get a folder in `~/Games` named like the download's top folder
(`SpeedBreaker-Carbon/`, or `SpeedBreaker-Carbon-test/` for a test build),
next to SpeedBreaker's own: neither replaces the other. No `:`, `;` or `$` in
the names of the folders it's in.

## 2. Add it to Steam

In Desktop Mode: Steam > Games > Add a Non-Steam Game to My Library > Browse.
Set the file type to "All Files", pick `speedbreaker.sh` in that folder, then
Add Selected Programs, and rename the shortcut in its Properties. Leave its
Compatibility setting off: this is a Linux program, and Proton can't run it.
Docked to a TV or monitor: Properties > General > Game Resolution: Native.

## 3. Your disc image

Put the `.iso` in Downloads, your home folder, Desktop, Documents, Games or on
the SD card (the top folder or one folder down). The first run finds it there
and installs the game from it (78 files, about 5.7 GB; every file is checked).
Only the Collector's Edition (USA) is accepted. From a terminal, the same:
`speedbreaker.sh --install <your image>.iso`, and `speedbreaker.sh --verify
<your image>.iso` checks an image without installing.

## Controls and screen

- Settings menu: hold View and press Menu (keyboard: F1). The game pauses
  while it's open.
- Steam Deck OLED: set the refresh rate to 60 Hz (Quick Access > Performance
  > Refresh Rate). The game runs at 60 frames a second; at 90 Hz its frames
  can't be shown evenly.

## Where things are

- Saves, `settings.toml`, logs and the installed game (`game/`):
  `~/.local/share/speedbreaker-carbon/`. Shader caches (safe to delete):
  `~/.cache/speedbreaker-carbon/`. Most Wanted's `speedbreaker` folders are
  never read or written.
- Each run writes `last-run.log` in this folder (the run before is kept as
  `previous-run.log`). `speedbreaker.sh --where` prints where the game, your
  data and the caches are.
- To uninstall: delete this folder, `~/.local/share/speedbreaker-carbon/`
  (your saves too) and `~/.cache/speedbreaker-carbon/`, and remove the
  shortcut from Steam.

The licenses of the game and of each bundled library are in `licenses/`.
