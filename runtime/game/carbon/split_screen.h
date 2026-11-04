// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// NFS: Carbon's split-screen test (game/split_screen.h's GameSaysSplit; the
// hook is game/carbon/split_screen.cpp; GitHub speedbreaker-carbon #5).
//
// Most Wanted's test reads its FEDatabase (0x82A2C684: players at +0, Quick
// Race's flag at +300). Carbon keeps no player count there: the front end
// keeps "split screen" as one bool, key 3015 of its settings store (the
// object at *0x82B8879C; vtable 0x8206FEF4, Get at +24 = sub_821F1F90, Set at
// +28 = sub_821F24D0), held in the byte at 0x82C64320:
//   - Set(3015, v) stores v > 0 there (sub_821F24D0; sub_821F4230 the same);
//     Get(3015) reads it back as 0 or 1 (sub_821F1F90's case 3015). Nothing
//     else reads or writes the byte.
//   - The front end's state managers set it to 1 as two-player starts (the
//     handlers sub_82671CE0, state 13, and sub_8266E518, state 11: before
//     "PLAYER 2 Please press START on second controller."), and set it to 0
//     back at their first state (sub_82673150, state 1; sub_82643540).
//   - A race reads it as it loads: sub_824555E0 makes the race's mode 1 (two
//     players; 0 one, 2 online) from it, which sub_82529EE8 stores at
//     0x82C65418 (the Lua binding IsSplitScreen, sub_82408E50, is that mode
//     == 1), and the loader takes Global\InGameSplitScreen.bun on it
//     (sub_82455D80).
// So the byte is the whole split-screen stretch, as Most Wanted's test is.
// Seen live (fix/carbon-5, 2026-11-04, two virtual pads on a Mac): it goes
// to 1 the moment Quick Race Options' Game Mode is set to Split Screen
// (before both car selects and "PLAYER 2 Please press START on second
// controller."), stays 1 through the race, and goes to 0 when Game Mode is
// set back to Solo, when the player backs out of Quick Race Options to the
// race map, and when a split race is exited (back at the map). B on Player
// 2's screen returns to player 1's car select, still split screen.
//
// tests/carbon_split_screen_test.cpp checks these facts against ppc-carbon/.
#pragma once
#include <cstdint>

namespace game::carbon_split
{
    constexpr uint32_t kSplitFlag = 0x82C64320;  // byte: the store's key 3015
    constexpr uint32_t kSplitKey = 3015;
    constexpr uint32_t kRaceMode = 0x82C65418;   // word: 1 = two players (set as a race loads)

    // The game's test, on the byte (Set stores 0 or 1; anything else nonzero
    // reads as 1 too, as Get does).
    constexpr bool Decode(uint8_t flag)
    {
        return flag != 0;
    }
}
