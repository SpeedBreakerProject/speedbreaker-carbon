# Changelog

What changed in each SpeedBreaker: Carbon release, newest first. Each release's notes on GitHub
start from its entry here, and the game shows the entry once, the first time a new version starts
(offline: it's built into the game).

## v0.1.1

### What's new

- **HDR output on a Mac with an HDR display** (Settings > Display > HDR Output, off by default):
  the game's highlights — the sun, headlights, lit signs — reach the display's extended dynamic
  range. This settings overlay stays at standard brightness; the game's own front end and HUD are
  part of the picture and follow the highlights. HDR Format chooses Auto, Extended Linear, or
  HDR10 (PQ); Auto uses Extended Linear where the screen reports HDR capability, and stays at
  standard range where it doesn't.
- **HDR Strength** (Settings > Display): 2x (200 nits in HDR10) by default, 3x, or 4x. A
  per-frame average limiter lowers the peak toward SDR when the scene is mostly white, so a
  boot flash or a full-screen logo does not become a 4x flash.

### More about it

- HDR10 sends PQ with BT.2020 primaries and static metadata; the display's HDR mode still follows
  macOS. EDR follows the display's current headroom every frame.
- Bug-report screenshots and NFSMW_CHECK_PRESENT_SEC captures stay standard-range; the PPM decodes
  PQ from BT.2020 back to Rec.709. NFSMW_HDR=1, NFSMW_HDR_FORMAT=auto|edr|hdr10 and
  NFSMW_HDR_STRENGTH=2x|3x|4x set them for a run; iOS and Linux don't offer them.

## v0.1.0

### What's new

- **Need for Speed: Carbon, native, for its 20th anniversary.** SpeedBreaker: Carbon runs the Xbox
  360 game recompiled for your computer, on SpeedBreaker's engine: a Mac with Apple silicon,
  SteamOS (Steam Deck, Steam Machine and other x86-64 Linux PCs) and the Steam Frame, as a big
  flat screen in the headset (not VR).
- **Windows (beta).** Windows 10 and 11 PCs (64-bit, with an AVX2 CPU) get SpeedBreaker: Carbon
  as a beta, on its own pre-release download. So far it has been tested on one laptop with Intel
  graphics.
- **iPhone and iPad (experimental).** SpeedBreaker Carbon is its own app, beside SpeedBreaker for
  Most Wanted: sideload the IPA or build it with Xcode. A controller or keyboard plays it. So far
  it has been tested on an iPhone 18 Pro Max.
- **Your own disc.** The first start installs the game from your copy of the USA Collector's
  Edition: a disc image (.iso) or the files extracted from one, checked file by file. Other
  editions aren't supported yet. No game data comes with SpeedBreaker: Carbon.
- **Any screen.** Fill Screen fills 16:10, 21:9 and taller screens without stretching, and renders
  the screen's own shape (a Steam Deck draws 1280x800). Internal Resolution renders above the
  360's 720p (Auto picks from the screen and the GPU), with anti-aliasing, sharpening and
  anisotropic filtering.
- **As on the 360.** A steady 60 fps, the movies paced as on the console, the nitrous light trails
  at the 360's length, and the 360's display gamma, so the game's own Brightness slider works.
- **Controllers and keyboard.** Xbox, PlayStation and other controllers with rumble, and a
  keyboard you can rebind. F1 (or Back + Start) opens the settings over the game; Quit Game is
  there too.
- **When something goes wrong**, Settings > Advanced > Save Bug Report puts the logs and a
  screenshot in one .zip for you to send. Check for Updates asks GitHub for a newer
  SpeedBreaker: Carbon, only when you press it.

### More about it

- Not in this first release: two-player split screen, Smooth Motion and the language choice of
  SpeedBreaker for Most Wanted. Carbon plays in its disc's language.
- Your saves and settings live in their own folder, apart from SpeedBreaker for Most Wanted's:
  both can be installed side by side.
