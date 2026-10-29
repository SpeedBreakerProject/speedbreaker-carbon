// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See split_screen.h.
#include <stdafx.h>
#include "split_screen.h"

#include <kernel/memory.h>

namespace
{
    const bool s_enabled = [] {
        const char* v = std::getenv("NFSMW_SPLIT_SCREEN");
        const bool on = !(v && v[0] == '0');
        if (!on)
            fprintf(stderr, "[split] NFSMW_SPLIT_SCREEN=0: one controller only, and no split-screen cut in the renderer (v0.1.7)\n");
        return on;
    }();

    uint32_t LoadBig32(const uint8_t* host)
    {
        uint32_t v = __atomic_load_n(reinterpret_cast<const uint32_t*>(host), __ATOMIC_RELAXED);
        return ByteSwap(v);
    }
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
        const uint32_t db = LoadBig32(g_memory.base + kDatabasePointer);
        if (!PlausibleObject(db))
            return false;
        const uint8_t players = __atomic_load_n(g_memory.base + db + kPlayersOffset, __ATOMIC_RELAXED);
        if (players != 2)
            return false;
        return Decode(players, LoadBig32(g_memory.base + db + kFlagsOffset));
    }
}
