// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Covers (NFSMW_LOG_COVERS=1, a diagnostic): find the draws and pictures
// that leave a dark box, or a layer meant to cover the whole screen, short
// of the picture's edges, so a play-through logs every such place once
// instead of a player spotting each one. Vulkan-free: tests/covers_test.cpp
// checks it alone.
//
// Two detectors share these rules:
// - Draws (gpu/renderer.cpp, ClassifyQuad): each quad of a front-end draw
//   (the front-end matrix, ePolys at fetch 95: every fade, dim, black frame,
//   panel and backdrop of the menus, the loading screens and the in-game
//   transitions), after the edge-bar and Vert+/Hor+ rules have moved it,
//   as host pixels of the frame against the guest viewport the presenter
//   shows. Logged once per draw site: the guest code that built the quad
//   (game/cover_callers.h: the return addresses above the ePoly writer).
// - Pictures (video/presenter.cpp, FindInsetBox): every presented picture,
//   scaled down, for a dark box whose four edges all lie inside it with a
//   lit border round it, or a dark layer across it lit above and below (or
//   beside) it (whatever drew it: a movie, a target shown before it was
//   drawn, a 2D quad).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gpu::covers
{
    // Host pixels, [x0, x1) x [y0, y1).
    struct Rect
    {
        int32_t x0, y0, x1, y1;
    };

    inline int64_t Area(const Rect& r)
    {
        return r.x1 > r.x0 && r.y1 > r.y0 ? int64_t(r.x1 - r.x0) * (r.y1 - r.y0) : 0;
    }

    inline Rect Intersect(const Rect& a, const Rect& b)
    {
        return { std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1) };
    }

    enum class Kind : uint8_t
    {
        None,
        InsetBox,     // dark, most of the picture, all four edges inside it
        DarkShort,    // dark, most of the picture, one to three edges short of it
        LayerShort,   // authored to cover the whole 16:9 frame (any colour), short of the picture's edges
    };

    inline const char* KindName(Kind k)
    {
        switch (k)
        {
        case Kind::InsetBox: return "a dark box inside the picture";
        case Kind::DarkShort: return "a dark layer short of the picture's edges";
        case Kind::LayerShort: return "a full-screen layer short of the picture's edges";
        default: return "none";
        }
    }

    struct Rule
    {
        uint32_t darkMax = 0x30;      // each of R, G, B (vertex colour times texel), 0..255
        uint32_t spread = 0x18;       // the four vertices' colours within this of each other (near-uniform)
        uint32_t visibleAlpha = 0x40; // the least vertex alpha that counts (alpha-blended draws)
        float minShare = 0.5f;        // the quad's share of the picture's area (dark kinds)
        float layerReach = 0.999f;    // band units each way a full-screen layer is authored to (|x| and |y|)
        int32_t slack = 1;            // host pixels short of an edge that don't count
    };

    // One quad as the renderer sees it.
    struct Quad
    {
        Rect host;            // its box in the target's host pixels, after every rule, clipped to the draw's scissor
        Rect picture;         // the guest viewport in host pixels (what the presenter shows)
        float band[4];        // its box in band units before the rules: x0, x1, y0, y1 (+-1: the 16:9 frame's edges)
        uint32_t argb[4];     // vertex colours
        uint32_t texel = 0xFFFFFFFFu;  // its texture's darkest sample (ARGB; white when untextured or the 32x32 white texture)
        bool texelKnown = true;        // false: a texture it couldn't sample (its shape and colour unknown: never "dark")
        bool blended = true;  // alpha blending on (else opaque)
        bool filledOutside = false;  // an edge-bar fill draws or clears everything outside it opaque (black frames, backdrops)
    };

    struct Verdict
    {
        Kind kind = Kind::None;
        int32_t shortBy[4] = {};  // left, top, right, bottom: host pixels the visible box stops short of the picture
        float share = 0;          // its visible area / the picture's
        uint32_t minAlpha = 0, maxRgb = 0;
    };

    inline uint32_t Channel(uint32_t argb, int k)
    {
        return (argb >> (k * 8)) & 0xFF;
    }

    inline Verdict ClassifyQuad(const Quad& q, const Rule& rule = {})
    {
        Verdict v;
        const int64_t picture = Area(q.picture);
        if (!picture)
            return v;
        const Rect seen = Intersect(q.host, q.picture);
        v.share = float(double(Area(seen)) / double(picture));
        if (!Area(seen))
            return v;
        v.minAlpha = 0xFF;
        uint32_t lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 };
        for (const uint32_t c : q.argb)
        {
            if (q.blended)
                v.minAlpha = std::min(v.minAlpha, c >> 24);
            for (int k = 0; k < 3; k++)
            {
                // The front-end shader modulates: vertex colour times texel.
                const uint32_t ch = Channel(c, k) * Channel(q.texel, k) / 255;
                lo[k] = std::min(lo[k], ch);
                hi[k] = std::max(hi[k], ch);
            }
        }
        if (q.blended)
            v.minAlpha = v.minAlpha * (q.texel >> 24) / 255;
        v.maxRgb = std::max({ hi[0], hi[1], hi[2] });
        v.shortBy[0] = seen.x0 - q.picture.x0;
        v.shortBy[1] = seen.y0 - q.picture.y0;
        v.shortBy[2] = q.picture.x1 - seen.x1;
        v.shortBy[3] = q.picture.y1 - seen.y1;
        int shortEdges = 0;
        for (int32_t& s : v.shortBy)
        {
            if (s <= rule.slack)
                s = 0;
            shortEdges += s > 0;
        }
        if (!shortEdges || q.filledOutside || v.minAlpha < rule.visibleAlpha)
            return v;
        const bool uniform = hi[0] - lo[0] <= rule.spread && hi[1] - lo[1] <= rule.spread && hi[2] - lo[2] <= rule.spread;
        const bool dark = q.texelKnown && uniform && v.maxRgb <= rule.darkMax;
        if (dark && v.share >= rule.minShare)
            v.kind = shortEdges == 4 ? Kind::InsetBox : Kind::DarkShort;
        else if (-q.band[0] >= rule.layerReach && q.band[1] >= rule.layerReach && -q.band[2] >= rule.layerReach && q.band[3] >= rule.layerReach)
            v.kind = Kind::LayerShort;
        return v;
    }

    // A presented picture, scaled down: 8-bit luma rows.
    struct Luma
    {
        const uint8_t* px;
        int32_t w, h, stride;
        uint8_t at(int32_t x, int32_t y) const { return px[size_t(y) * stride + x]; }
    };

    struct BoxRule
    {
        uint8_t darkMax = 12;         // luma of a dark pixel
        float minShare = 0.25f;       // the box's share of the picture
        float maxShare = 0.97f;       // larger: a black picture with a stray border row (not a box)
        float insideDark = 0.995f;    // its interior's dark share
        int32_t ring = 3;             // pixels of border sampled just outside each edge
        float ringLit = 0.5f;         // the share of those that must be lit, on every side asked
        int32_t margin = 2;           // an edge this close to the picture's touches it
    };

    enum class BoxKind : uint8_t
    {
        None,
        Inset,  // all four edges inside the picture, lit all round
        BandY,  // across the whole width, lit above and below it (a full-screen layer short of the top and bottom)
        BandX,  // across the whole height, lit beside it
    };

    inline const char* BoxKindName(BoxKind k)
    {
        switch (k)
        {
        case BoxKind::Inset: return "a dark box inside the picture";
        case BoxKind::BandY: return "a dark layer short of the picture's top and bottom";
        case BoxKind::BandX: return "a dark layer short of the picture's sides";
        default: return "none";
        }
    }

    struct Box
    {
        bool found = false;
        BoxKind kind = BoxKind::None;
        Rect box{};
        float share = 0;
        float ringLit[4] = {};  // left, top, right, bottom (-1: on the picture's edge)
    };

    // A dark region around the picture's centre that stops short of the
    // picture's edges with the picture lit just outside it: on all four sides
    // (a box), or above and below it with its sides on the picture's (a band:
    // a full-screen layer short of the top and bottom), or beside it (short
    // of the sides). Grown from the centre along the middle row and column
    // while dark, then checked: its interior dark, the lit border on every
    // side that isn't the picture's edge.
    inline Box FindInsetBox(const Luma& l, const BoxRule& rule = {})
    {
        Box b;
        const int32_t cx = l.w / 2, cy = l.h / 2;
        if (l.w < 16 || l.h < 16 || l.at(cx, cy) > rule.darkMax)
            return b;
        int32_t x0 = cx, x1 = cx, y0 = cy, y1 = cy;
        while (x0 > 0 && l.at(x0 - 1, cy) <= rule.darkMax)
            x0--;
        while (x1 < l.w - 1 && l.at(x1 + 1, cy) <= rule.darkMax)
            x1++;
        while (y0 > 0 && l.at(cx, y0 - 1) <= rule.darkMax)
            y0--;
        while (y1 < l.h - 1 && l.at(cx, y1 + 1) <= rule.darkMax)
            y1++;
        b.box = { x0, y0, x1 + 1, y1 + 1 };
        const bool onL = x0 < rule.margin, onR = x1 >= l.w - rule.margin, onT = y0 < rule.margin, onB = y1 >= l.h - rule.margin;
        BoxKind kind = BoxKind::None;
        if (!onL && !onR && !onT && !onB)
            kind = BoxKind::Inset;
        else if (onL && onR && !onT && !onB)
            kind = BoxKind::BandY;
        else if (onT && onB && !onL && !onR)
            kind = BoxKind::BandX;
        else
            return b;
        b.share = float(double(Area(b.box)) / (double(l.w) * l.h));
        if (b.share < rule.minShare || b.share > rule.maxShare)
            return b;
        int64_t dark = 0, all = 0;
        for (int32_t y = y0; y <= y1; y += 2)
            for (int32_t x = x0; x <= x1; x += 2)
            {
                dark += l.at(x, y) <= rule.darkMax;
                all++;
            }
        if (double(dark) < rule.insideDark * double(all))
            return b;
        // The ring just outside each edge, along that edge's middle 80%.
        auto lit = [&](int32_t xa, int32_t xb, int32_t ya, int32_t yb) {
            int64_t n = 0, on = 0;
            for (int32_t y = std::max(ya, 0); y < std::min(yb, l.h); y++)
                for (int32_t x = std::max(xa, 0); x < std::min(xb, l.w); x++)
                {
                    n++;
                    on += l.at(x, y) > rule.darkMax;
                }
            return n ? float(double(on) / double(n)) : 0.0f;
        };
        const int32_t mx = (x1 - x0) / 10, my = (y1 - y0) / 10;
        b.ringLit[0] = onL ? -1.0f : lit(x0 - rule.ring, x0, y0 + my, y1 - my);
        b.ringLit[1] = onT ? -1.0f : lit(x0 + mx, x1 - mx, y0 - rule.ring, y0);
        b.ringLit[2] = onR ? -1.0f : lit(x1 + 1, x1 + 1 + rule.ring, y0 + my, y1 - my);
        b.ringLit[3] = onB ? -1.0f : lit(x0 + mx, x1 - mx, y1 + 1, y1 + 1 + rule.ring);
        b.found = true;
        for (const float r : b.ringLit)
            if (r >= 0.0f && r < rule.ringLit)
                b.found = false;
        b.kind = b.found ? kind : BoxKind::None;
        return b;
    }
}
