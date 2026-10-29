// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See game/ultrawide.h.
//
// NFS: Carbon's Hor+ and Vert+, Most Wanted's design (game/ultrawide.cpp) on
// Carbon's code: the game keeps rendering its 1280x720 frame, the presenter
// stretches it to fill a screen of another shape, and the view is narrowed
// by the same factor so the stretch doesn't distort it.
//
// Two mid-asm hooks (config/carbon/carbon.toml):
//   - UltrawideProjection, before `stfs f5,0(r29)` at 0x822CED84 in the view
//     setup (sub_822CEA60, Most Wanted's sub_8243EAC0). Carbon builds two
//     projections there, at view + 64 and view + 128 (the second with a
//     farther depth range), from the same scales: f5 = P[0][0] = 1 / tan(half
//     the horizontal FOV), stored at 0x822CED84 and 0x822CEDDC, and f11 =
//     1 / tan(half the vertical FOV), negated in place at 0x822CED98 and
//     stored as P[1][1] at 0x822CED9C and 0x822CEDE0. Neither register is
//     written between this hook and the second store, so one hook scales
//     both matrices; the frustum and the LOD scale (sub_822CE870, from the
//     stored matrix) follow. r7 is the view object; its id at +8 (the game's
//     own 16:9 widening, 0x822CEBBC, tests it for 1, 2, 4, 5 and 21:
//     EVIEW_PLAYER1/2, their reflections and the shadow matte, by the name
//     table at 0x82B73B94 with ids from 1). Only those views are changed,
//     as in Most Wanted: the mirror (3), the shadow maps and the
//     environment cube keep theirs.
//   - UltrawideFrontEnd, at the branch at 0x8250AD60 that ends the
//     widescreen case of the front-end matrix (sub_8250AC68, Most Wanted's
//     sub_82440D80, the same code): f13 its x scale (0.92 safe area x 0.75)
//     and f0 its y scale (0.92). When the matrix it leaves differs from the
//     last (the window changed shape, or the first frames), it also clears
//     the game's cached matrix pointer, as Most Wanted's does
//     (game/front_end_matrix.h): Carbon's upload, sub_824F4E98 (Most
//     Wanted's sub_82453768), keeps its last call's view, matrix and object
//     pointers at 0x82C65270, +4, +8 and returns at once when a call brings
//     the same three (and its fourth argument, a force flag, is 0, as the
//     front end passes it), so without this a window changed during the boot
//     movies kept the old shape's matrix. NFSMW_FE_RESEND=0 leaves the cache
//     alone (an A/B switch).
// And one function override (no ppc/ regeneration):
//   - the rear-view mirror's and the crew's picture-in-picture rectangle
//     builder, sub_82505248, called only by their draws (sub_825053A8 at
//     0x825054C0, sub_825055F0 at 0x825056F0). Carbon renders the mirror as
//     a view of its own (EVIEW_PLAYER1_RVM, id 3, a 320x240 target) and the
//     crew's video as another, then draws each on screen as one quad
//     through RVMPIP.FX with the RVM_MASK or PIP_MASK texture, in
//     coordinates the CPU builds from f1 = left edge and f2 = top or bottom
//     edge (0..1 of the screen), f3 = signed width, f4 = height: the mirror
//     0.638, 0.785, -0.275, 0.128 (mirrored, as Most Wanted's 0.638 and
//     -0.275), the picture-in-picture 0.04, 0.68, 0.191, 0.255 (16:9; other
//     values for 4:3). The front-end matrix never touches them, so under
//     Hor+ the stretch made the mirror 1.3x too wide for its frame (a HUD
//     sprite) and under Vert+ it slid off its frame. Narrowed about the
//     screen centre like the HUD, as Most Wanted's UltrawideMirror.
// Movies: the XMV player reads its display aspect, 16/9, from 0x820103C0
// (`lfs f0` at 0x827B762C in sub_827B7590, its only reader), as Most
// Wanted's from 0x8200FAC0; SetAspectScale writes the aspect the frame is
// shown at there, so a movie is pillarboxed or letterboxed to 16:9.
#include <stdafx.h>
#include <game/ultrawide.h>
#include <game/front_end_matrix.h>

#include <kernel/function.h>

extern "C" PPC_FUNC(__imp__sub_82505248);

namespace
{
    constexpr uint32_t kMovieDisplayAspect = 0x820103C0;
    // Per-view table: 0x82C721D0 + id * 480, the view's target at +464,
    // its width and height at +16 and +20.
    constexpr uint32_t kViewTable = 0x82C721D0, kViewStride = 480, kViewTarget = 464;

    struct Scales
    {
        float x, y;
    };
    std::atomic<Scales> s_scales{ Scales{ 1.0f, 1.0f } };
    std::atomic<bool> s_hooksSeen{ false };
    std::atomic<float> s_frontEndX{ 0.0f };
    std::atomic<float> s_frontEndY{ 0.0f };

    bool WidenedViewId(uint32_t id)
    {
        return id == 1 || id == 2 || id == 4 || id == 5 || id == 21;
    }

    void LogView(uint32_t id, bool widened)
    {
        static std::atomic<uint64_t> seen{ 0 };
        if (id >= 64 || (seen.fetch_or(1ull << id) & (1ull << id)))
            return;
        uint32_t target = ByteSwap(*static_cast<const uint32_t*>(g_memory.Translate(kViewTable + id * kViewStride + kViewTarget)));
        uint32_t w = 0, h = 0;
        if (target)
        {
            w = ByteSwap(*static_cast<const uint32_t*>(g_memory.Translate(target + 16)));
            h = ByteSwap(*static_cast<const uint32_t*>(g_memory.Translate(target + 20)));
        }
        fprintf(stderr, "[uw] view %u: target %ux%u%s\n", id, w, h, widened ? ", widened" : "");
    }

    void ScaleRegister(PPCRegister& f, float scale)
    {
        f.f64 = double(float(f.f64) * scale);
    }

    // About the screen centre (0.5 of it), for the mirror's 0..1 edges.
    void ScaleAboutCentre(PPCRegister& f, float scale)
    {
        f.f64 = double(0.5f + (float(f.f64) - 0.5f) * scale);
    }

    // sub_824F4E98's cache of its last call (view, matrix, object): Carbon's
    // place for Most Wanted's game::front_end::kMatrixCache.
    constexpr uint32_t kMatrixCache = 0x82C65270;
    constexpr uint32_t kMatrixCachePointer = kMatrixCache + 4;
    game::front_end::MatrixTracker s_frontEndMatrix;  // the game's render thread
    const bool s_frontEndResend = [] {
        const char* v = std::getenv("NFSMW_FE_RESEND");
        return !(v && v[0] == '0');
    }();

    // The hook changed the front end's matrix: clear the cached matrix
    // pointer so the call right after the hook uploads it.
    void ResendIfChanged(float x, float y)
    {
        if (!s_frontEndMatrix.Changed(x, y) || !s_frontEndResend)
            return;
        *static_cast<uint32_t*>(g_memory.Translate(kMatrixCachePointer)) = 0;
        static uint32_t logged = 0;
        if (++logged <= 16)
            fprintf(stderr, "[uw] front-end matrix x %.4f y %.4f: sent to the game again%s\n", x, y,
                logged == 16 ? " (no more of these lines)" : "");
    }
}

namespace game
{
    void SetAspectScale(float scaleX, float scaleY)
    {
        const Scales next{ std::clamp(scaleX, 0.1f, 1.0f), std::clamp(scaleY, 0.5f, 1.0f) };
        const Scales last = s_scales.exchange(next, std::memory_order_relaxed);
        if (last.x != next.x || last.y != next.y)
            fprintf(stderr, "[uw] view scale x %.4f y %.4f\n", next.x, next.y);
        float aspect = (16.0f / 9.0f) * next.y / next.x;
        uint32_t bits;
        memcpy(&bits, &aspect, sizeof(bits));
        *static_cast<uint32_t*>(g_memory.Translate(kMovieDisplayAspect)) = ByteSwap(bits);
    }

    float AspectScaleX()
    {
        return s_scales.load(std::memory_order_relaxed).x;
    }

    float AspectScaleY()
    {
        return s_scales.load(std::memory_order_relaxed).y;
    }

    bool HooksActive()
    {
        return s_hooksSeen.load(std::memory_order_relaxed);
    }

    bool TallHooksActive()
    {
        return s_hooksSeen.load(std::memory_order_relaxed);
    }

    float FrontEndScaleX()
    {
        return s_frontEndX.load(std::memory_order_relaxed);
    }

    float FrontEndScaleY()
    {
        return s_frontEndY.load(std::memory_order_relaxed);
    }

    // The minimap's hiding-spot icon, parked below the front end and pulsing
    // across the band's edge (gpu/edge_bars.h Parked).
    bool FrontEndParksByCentre()
    {
        return true;
    }
}

void UltrawideProjection(PPCRegister& r7, PPCRegister& f5, PPCRegister& f11)
{
    if (!s_hooksSeen.load(std::memory_order_relaxed))
        s_hooksSeen.store(true, std::memory_order_relaxed);
    const Scales scale = s_scales.load(std::memory_order_relaxed);
    const uint32_t id = ByteSwap(*static_cast<const uint32_t*>(g_memory.Translate(r7.u32 + 8)));
    const bool widen = WidenedViewId(id);
    LogView(id, widen);
    if (!widen || (scale.x == 1.0f && scale.y == 1.0f))
        return;
    if (scale.x != 1.0f)
        ScaleRegister(f5, scale.x);
    if (scale.y != 1.0f)
        ScaleRegister(f11, scale.y);  // its magnitude: negated into P[1][1] after this
}

void UltrawideFrontEnd(PPCRegister& f13, PPCRegister& f0)
{
    const Scales scale = s_scales.load(std::memory_order_relaxed);
    if (scale.x != 1.0f)
        ScaleRegister(f13, scale.x);
    if (scale.y != 1.0f)
        ScaleRegister(f0, scale.y);
    s_frontEndX.store(float(f13.f64), std::memory_order_relaxed);
    s_frontEndY.store(float(f0.f64), std::memory_order_relaxed);
    ResendIfChanged(float(f13.f64), float(f0.f64));
}

// The mirror's and the picture-in-picture's rectangle (sub_82505248): f1
// left edge, f2 top or bottom edge, f3 signed width, f4 height.
PPC_FUNC(sub_82505248)
{
    const Scales scale = s_scales.load(std::memory_order_relaxed);
    if (scale.x != 1.0f)
    {
        ScaleAboutCentre(ctx.f1, scale.x);
        ScaleRegister(ctx.f3, scale.x);
    }
    if (scale.y != 1.0f)
    {
        ScaleAboutCentre(ctx.f2, scale.y);
        ScaleRegister(ctx.f4, scale.y);
    }
    __imp__sub_82505248(ctx, base);
}
