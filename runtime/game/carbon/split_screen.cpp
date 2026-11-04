// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// NFS: Carbon's side of game/split_screen.h: the game's own split-screen
// test, its front end's "split screen" byte (game/carbon/split_screen.h;
// GitHub speedbreaker-carbon #5). With it the shared runtime does for Carbon
// what it does for Most Wanted: hid/input.cpp shows the game the other
// controllers as XInput users 1-3 while the game is in split screen (so
// "Player 2 press START" takes the second controller's START), and the
// renderer cuts each untiled draw to its own view (gpu/split_views.h).
// NFSMW_SPLIT_SCREEN=0: neither (v0.1.1: one controller only).
#include <stdafx.h>
#include <game/split_screen.h>
#include <game/carbon/split_screen.h>

#include <kernel/memory.h>

namespace
{
    const bool s_enabled = [] {
        const char* v = std::getenv("NFSMW_SPLIT_SCREEN");
        const bool on = !(v && v[0] == '0');
        if (!on)
            fprintf(stderr, "[split] NFSMW_SPLIT_SCREEN=0: one controller only, and no split-screen cut in the renderer (Carbon v0.1.1)\n");
        return on;
    }();
}

namespace game::split_screen
{
    bool Enabled()
    {
        return s_enabled;
    }

    bool GameSaysSplit()
    {
        if (!s_enabled || !g_memory.base)
            return false;
        // One plain byte load, racing the game's writes harmlessly.
        return game::carbon_split::Decode(__atomic_load_n(g_memory.base + game::carbon_split::kSplitFlag, __ATOMIC_RELAXED));
    }
}
