// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// NFS: Carbon: no split screen yet (game/split_screen.h). Most Wanted's
// (game/split_screen.cpp) reads its own front-end database (0x82A2C684,
// players at +0, Quick Race's flag at +300); Carbon's isn't mapped. So
// input stays one player (any controller drives player 1, other XInput
// users unconnected, as in v0.1.7) and the renderer never cuts views.
#include <stdafx.h>
#include <game/split_screen.h>

namespace game::split_screen
{
    bool Enabled() { return false; }
    bool GameSaysSplit() { return false; }
}
