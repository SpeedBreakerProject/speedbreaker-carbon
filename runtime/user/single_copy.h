// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// One copy of the game per data folder (2026-10-28). A Steam Deck woken
// from sleep with the game still open started its shortcut a second time:
// the first copy, paused and out of sight, kept drawing at full rate, the
// one being played got 41-45 fps instead of 60, the battery drained twice
// as fast, and both had the same saves open. Now a second copy says the
// game is already running and closes; the first is left alone.
//
// The claim is an exclusive lock (platform::LockFolder: flock on POSIX, a
// Global\ named mutex on Windows) on the data folder's logs/ folder, held
// until the process ends: an exit or a crash lets go by itself, so a stale
// claim can't keep the game from starting. Not the data folder itself: the
// installer locks the folder it installs into (the data folder, by default)
// for one install at a time, and the game's own first-run installer, in this
// same process, would find it busy. A folder that can't be locked (a file
// system without locks, a portable folder that can't be written) leaves the
// game unguarded rather than refused.
//
// Per data folder, so Most Wanted and Carbon (each their own folder) run side
// by side, and so do two portable copies. Not on iOS, where an app runs once.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace singlecopy
{
    enum class Claim
    {
        Ours,        // this copy holds it now, until it ends
        OtherCopy,   // another copy of the game holds it: this one should close
        Unguarded,   // the folder couldn't be locked: run anyway
    };

    // Locks `data`/logs (making the folder if it isn't there). Ours keeps the
    // lock for the rest of the process.
    Claim ClaimDataFolder(const std::filesystem::path& data);

    // The page the second copy shows before it closes.
    struct Message
    {
        std::string title;
        std::vector<std::string> paragraphs;
    };
    Message AlreadyRunning(const char* appName);
}
