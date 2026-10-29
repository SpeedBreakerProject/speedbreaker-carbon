// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// NFSMW_LOG_COVERS=1 (gpu/covers.h): which guest code built a front-end
// quad. A 2D quad of the front end (menus, HUD, fades, loading screens) is
// written as four 32-byte ePolys (x, y, z, colour, two uv pairs) by
// sub_824457B0 (from an ePoly source in pixels: x * k / f1 - k, the colour's
// bytes R, G, B, A as one ARGB word), called by sub_82445C98 (into the poly
// batcher's slot, sub_8245B588, or a buffer drawn at once) and
// sub_82445FC8. While the log is on, a hook there keeps the return
// addresses above the call (the caller's, and six more up the stack's back
// chain: each function here saves its LR at its entry SP - 8) by the
// quad's contents (each vertex's x, y, z and colour: what the GPU then
// reads), and the renderer looks a quad up by the same words when it logs
// one (identical quads of two callers name the later: diagnostic quality).
#pragma once

#include <cstdint>
#include <string>

namespace game::covers
{
    bool Enabled();

    struct Chain
    {
        uint32_t lr[8] = {};
        uint32_t n = 0;
    };

    // A quad's key: its four ePolys' x, y, z and colour words (host values of
    // the guest's big-endian words), hashed.
    uint64_t QuadKey(const uint32_t words[16]);

    // The hook's: the chain that built the quad with this key.
    void Note(uint64_t key, const Chain& chain);

    // Quads noted so far (the hook ran).
    uint64_t Noted();

    // The chain recorded for the quad with this key (the latest of that
    // content: identical quads share one).
    bool CallerOf(uint64_t key, Chain& out);

    // "sub_8243F2A0+0x5C <- sub_82441E10+0x1A8 <- ..." (the functions holding
    // each return address).
    std::string Describe(const Chain& chain);
}
