// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The Xbox 360 release's two-player split screen (Quick Race > Split Screen;
// the PC release has none). The game does everything itself: the "Player 2
// press START" screen, a car select and a HUD for each player, two views of
// the world. The runtime has two jobs, both keyed to the game's own state:
//   - hid/input.cpp shows the game a second controller (XInput user 1, and
//     2-3 for more pads) only while the game is in split screen
//     (hid/player_slots.h), so single player sees XInput user 0 alone, as
//     before;
//   - gpu/renderer.cpp cuts each untiled draw to its own view
//     (gpu/split_views.h), so player 1's view can't draw over player 2's.
//
// The game's split-screen test, inline in 23 functions (front-end screens,
// the in-game loader of Global\InGameSplitScreen.bun, the track streamer,
// cutscenes, pause and post-race, audio streams): its FEDatabase object, a
// pointer at 0x82A2C684, holds the number of players in byte 0 and flags in
// the word at +300, bit 2 (& 4) = a quick race:
//     lwz r7,300(rDB); rlwinm r6,r7,0,29,29; ...; lbz r5,0(rDB); cmplwi r5,2
// (e.g. sub_823B2EF8). Quick Race's accept handler (sub_8255D328) writes 1
// player for Quick Play and Custom Race, 2 for Split Screen, the moment it
// is chosen: before "Player 2 press START".
//
// NFSMW_SPLIT_SCREEN=0: neither job (v0.1.7: one controller, and the
// renderer's fallback to the whole target; the game then waits at "Player 2
// press START").
#pragma once
#include <cstdint>

namespace game::split_screen
{
    constexpr uint32_t kDatabasePointer = 0x82A2C684;  // FEDatabase*
    constexpr uint32_t kPlayersOffset = 0;             // byte: number of players
    constexpr uint32_t kFlagsOffset = 300;             // big-endian word
    constexpr uint32_t kQuickRaceFlag = 4;

    // The game's test, on the two fields.
    constexpr bool Decode(uint8_t players, uint32_t flags)
    {
        return players == 2 && (flags & kQuickRaceFlag) != 0;
    }

    // Where a FEDatabase pointer may point: guest heap or image memory (the
    // 4 KB and 64 KB virtual heaps, the image, the physical window). Anything
    // else (0 before the front end starts) is no database: not split screen.
    constexpr bool PlausibleObject(uint32_t address)
    {
        if (address & 3)
            return false;  // an object's word at +300 is aligned
        return (address >= 0x00010000u && address < 0x7F000000u - 512u) ||
            (address >= 0x82000000u && address < 0x82CD0000u - 512u) ||
            (address >= 0xA0000000u && address < 0xC0000000u - 512u);
    }

    // NFSMW_SPLIT_SCREEN != 0 (default on).
    bool Enabled();

    // The game's own predicate, read now from guest memory (any thread; two
    // plain loads, racing the game's writes harmlessly). False when
    // disabled, or before guest memory or the database exists.
    bool GameSaysSplit();
}
