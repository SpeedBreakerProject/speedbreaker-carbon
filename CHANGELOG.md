# Changelog

What changed in each SpeedBreaker: Carbon release, newest first. Each release's notes on GitHub
start from its entry here, and the game shows the entry once, the first time a new version starts
(offline: it's built into the game).

## v0.1.1

### What's new

- **HDR with Carbon's own highlights.** On a Mac with an HDR display, on SteamOS and the Steam
  Deck OLED with HDR on, and on Linux and Windows PCs whose system is in HDR (Settings > Display >
  HDR Output, off by default, with HDR Strength and HDR Paper White as in Most Wanted), Carbon's
  picture keeps the light its scene holds above white: tail lights, street lights, lit windows,
  nitrous flames and sparks glow brighter than white instead of the 360's clipped highlights,
  rolled off into the display's headroom. Large bright areas, such as tyre smoke and the sun's
  halo, are held to a soft glow instead of a flat blown-out blob, while small lights keep their
  full brightness. The rest of the picture, the HUD and the game seen through the settings
  overlay stay as in SDR. On a Mac, HDR Format chooses Auto, Extended Linear or HDR10 (PQ). With
  HDR off, nothing changes. Built on danielgpinheiro's HDR output for Most Wanted (GitHub pull
  request #38, and Carbon's #2).
- **A sharp rear-view mirror.** The mirror now renders at the Internal Resolution, like the rest
  of the scene, so above 1x it's as sharp as the road behind it instead of the 360's
  stair-stepped 320x240.

### More about it

- Each part of the screen adds at most a tenth of white above SDR (NFSMW_HDR_LOCAL_BUDGET=0 turns
  that local limit off), on top of the frame-average limiter that dims a mostly white frame.
  NFSMW_SCALE_MIRROR=0 keeps the mirror at 1x.
- A possible fix for black grass and ground on AMD Radeon RX 6000 cards under Windows (in Most
  Wanted, the distant grass on the roadside verges: GitHub issue #67): shaders that look up their
  lighting per pixel now read it another way, which other GPUs draw the same as before. It isn't
  confirmed by testers yet; if you still see black patches on an AMD card, please report them
  (NFSMW_CONST_REL_RING=0: the old read).

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
