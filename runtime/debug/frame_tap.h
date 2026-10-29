// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// NFSMW_FRAME_TAP (frame_tap.cpp): part of the game's picture written to a
// file every few milliseconds, for test tools that watch the game.
#pragma once

namespace debug
{
    // Main thread, once the window is up: starts the tap's thread if
    // NFSMW_FRAME_TAP is set.
    void StartFrameTap();
}
