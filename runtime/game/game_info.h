// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Which game this build runs (SB_GAME, top-level CMakeLists.txt), as the
// player reads it: in crash, hang and bug reports and the installer.
// Literals, so they join others at compile time (the crash report's header
// is written from a signal handler).
#pragma once

// SB_GAME_RELEASE names the one release the build was recompiled from (the
// installer's manifest, runtime/install/manifest*.inc).
// SB_GAME_AUTO_RESOLUTION is what Settings > Graphics > Internal Resolution
// says Auto picks (gpu/renderer.cpp WantedRenderScale; Carbon's budget is
// smaller: video/scale_budget.h CarbonScaleBudget).
// SB_GAME_GAMMA_OFF ends Settings > Display > Xbox 360 Display Gamma's help:
// what Off shows (Carbon has no release from before the ramp to name).
// SB_GAME_UNSUPPORTED ends the installer's "not the version this build
// supports" message, and SB_GAME_SUPPORTED_DISC is the installer's line
// naming the one disc it takes (none for Most Wanted, which has one).
//
// The app as the player sees it, per game: SB_APP_NAME (the window's and the
// installer's title, the Quit prompt, Settings > Advanced, Check for Updates),
// SB_GITHUB_REPO (its releases, for Check for Updates, and its issues),
// SB_FILE_PREFIX (files it saves where both games' may meet: bug reports on
// the Desktop) and SB_IOS_APP_NAME (the iPhone and iPad app's name: its
// CFBundleDisplayName, scripts/ios/SpeedBreaker/Info*.plist, under its icon
// and as its folder in the Files app, where the installer looks for the disc
// image; the two games' apps install side by side). Most Wanted's are
// SpeedBreaker's, as before.
// SB_WINDOWS_EXE is the Windows program's file name (runtime/CMakeLists.txt
// names the exe and its .pdb after it; the installer's Command Prompt hint
// and scripts/windows/make_zip.py name it), so the two games' downloads
// never both hold a SpeedBreaker.exe: Most Wanted's stays SpeedBreaker.exe.
// SB_WINDOWS_LOGS is where Windows' message boxes say the log is (the data
// folder of user/paths.h kUserFolderName, as %LOCALAPPDATA% shows it).
#ifdef SB_GAME_CARBON
#define SB_GAME_TITLE "NFS: Carbon"
#define SB_GAME_NAME "Need for Speed: Carbon"
#define SB_GAME_RELEASE "the USA Collector's Edition for Xbox 360"
#define SB_GAME_AUTO_RESOLUTION "Auto: from the screen and the GPU (1440p: 1.5x on an integrated GPU such as a Mac's, 2x on a dedicated one; a Steam Deck: its 1280x800)."
#define SB_GAME_GAMMA_OFF "Off: the flatter picture, without the ramp."
#define SB_GAME_UNSUPPORTED " Other editions of the game (its standard edition, other regions) aren't supported yet."
#define SB_GAME_SUPPORTED_DISC "SpeedBreaker: Carbon supports the USA Collector's Edition of Need for Speed: Carbon. Other editions aren't supported yet."
#define SB_APP_NAME "SpeedBreaker: Carbon"
#define SB_GITHUB_REPO "SpeedBreakerProject/speedbreaker-carbon"
#define SB_FILE_PREFIX "speedbreaker-carbon"
#define SB_WINDOWS_EXE "SpeedBreaker-Carbon.exe"
#define SB_WINDOWS_LOGS "%LOCALAPPDATA%\\SpeedBreaker Carbon\\logs"
#define SB_IOS_APP_NAME "SpeedBreaker Carbon"
#else
#define SB_GAME_TITLE "NFS: Most Wanted"
#define SB_GAME_NAME "Need for Speed: Most Wanted"
#define SB_GAME_RELEASE "the USA Xbox 360 release"
#define SB_GAME_AUTO_RESOLUTION "Auto: from the screen (1440p: 2x; a Steam Deck: its 1280x800)."
#define SB_GAME_GAMMA_OFF "Off: the flatter picture of SpeedBreaker 0.1.3 and earlier."
#define SB_GAME_UNSUPPORTED ""
#define SB_APP_NAME "SpeedBreaker"
#define SB_GITHUB_REPO "SpeedBreakerProject/speedbreaker"
#define SB_FILE_PREFIX "speedbreaker"
#define SB_WINDOWS_EXE "SpeedBreaker.exe"
#define SB_WINDOWS_LOGS "%LOCALAPPDATA%\\SpeedBreaker\\logs"
#define SB_IOS_APP_NAME "SpeedBreaker"
#endif
