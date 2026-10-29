# SpeedBreaker: Carbon for Steam Frame

An unofficial static recompilation of the Xbox 360 version of Need for Speed:
Carbon (the USA Collector's Edition): a native port, not an emulator. A fan
project by project(u), not affiliated with, endorsed by or sponsored by
Electronic Arts or Valve.

This is the arm64 SteamOS download, for the Steam Frame (the Steam Deck,
Steam Machine and Linux PCs have their own, the x86-64 one). In the headset
the game plays on a big flat screen: it is not a VR mode. This folder has the
launcher, `speedbreaker.sh`, and in `lib/` the game program and the libraries
it needs. **Always start the game with `speedbreaker.sh`** (your Steam
shortcut does): the program in `lib/` doesn't find its libraries on its own.
It has **no game data**: you need your own disc image of your own Xbox 360
disc of the Collector's Edition (USA), an `.iso`; the game installs itself
from it on the first run. `VERSION` says which build this is. It is separate
from SpeedBreaker (Most Wanted): its own folder, its own saves and settings,
and either can be installed without the other.

It runs on the Frame's SteamOS as it is and needs no developer tools.

## 1. Open the Frame's desktop

Start the **Desktop** app from your Steam library: it opens a desktop
window in the headset, with a file manager (Dolphin) and a terminal
(Konsole).

## 2. Unpack

Download `SpeedBreaker-Carbon-steamos-arm64.tar.xz` (and its `.sha256` file)
into your Downloads folder. The Frame's file manager can't unpack it, so open
**Konsole** and run:

    mkdir -p ~/Games && tar -xf ~/Downloads/SpeedBreaker-Carbon-steamos-arm64.tar.xz -C ~/Games

You get `~/Games/SpeedBreaker-Carbon/`, next to SpeedBreaker's own: neither
replaces the other. To check the download first (optional):
`cd ~/Downloads && sha256sum -c SpeedBreaker-Carbon-steamos-arm64.tar.xz.sha256`
should say OK.

Keep the folder on the Frame's internal storage, and don't use `:`, `;` or
`$` in the names of the folders it's in: the game can't start from there.

To update later, unpack the newer download over the same folder (the same
command). Your Steam shortcut keeps working, and your saves, settings and the
installed game are kept (they're elsewhere, below). Settings menu > Advanced >
**Check for Updates** says whether a newer one is out. Nothing is checked
online unless you press Check for Updates.

## 3. Add it to Steam

In the file manager, open `~/Games/SpeedBreaker-Carbon`, right-click
`speedbreaker.sh` and choose **Add to Steam**. Or, in Konsole:

    steamos-add-to-steam ~/Games/SpeedBreaker-Carbon/speedbreaker.sh

You can rename the shortcut (it's called `speedbreaker.sh`) in its
Properties. Leave its Compatibility setting (Proton) off: this is a Linux
program, and Proton can't run it.

## 4. Your disc image

Put your `.iso` in **Downloads** (`~/Downloads`). The first run also looks in
your home folder, Desktop, Documents and Games. Only the Collector's Edition
(USA) is accepted.

## 5. Install and play

Start it from your Steam library (Non-Steam). The first run opens the
installer, with the image it found highlighted (marked "Ready to install"):
press A, then A again on Install. It checks the image first, then copies and
checks every file (78 files, about 5.7 GB). Afterwards you can delete the
`.iso`.

From Konsole, the same install:
`~/Games/SpeedBreaker-Carbon/speedbreaker.sh --install ~/Downloads/<your image>.iso`
(add `--dest <folder>` to put the game in a folder of your choice), and
`speedbreaker.sh --verify <your image>.iso` checks an image without
installing.

## Controls and settings

- The Frame's controllers work as an Xbox 360 controller.
- **Settings menu**: press **Menu + View** together. The game pauses while
  it's open. D-pad picks, left/right changes, A toggles, L1/R1 turn pages,
  B closes.
- **Frame rate**: if the picture isn't smooth in the headset, set
  **Display > Frame Rate** to **30**: the VR compositor shares the graphics
  chip.

## Where things are

- Saves, `settings.toml`, logs and the installed game (`game/`):
  `~/.local/share/speedbreaker-carbon/`. Shader caches (safe to delete):
  `~/.cache/speedbreaker-carbon/`. Most Wanted's `speedbreaker` folders are
  never read or written.
- Each run writes `last-run.log` in this folder (the run before is kept as
  `previous-run.log`). `speedbreaker.sh --where` prints where the game, your
  data and the caches are.
- To uninstall: delete this folder, `~/.local/share/speedbreaker-carbon/`
  (your saves too), `~/.cache/speedbreaker-carbon/` and the game's folder if
  you chose one, and remove the shortcut from Steam.

The licenses of the game and of each library are in `licenses/`.
