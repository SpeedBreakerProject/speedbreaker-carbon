// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Split screen's two views in one render target, untiled (gpu/renderer.cpp;
// header only, tests/split_views_test.cpp).
//
// The game draws both players' views of the world into one 4x MSAA scene
// target: player 1's viewport on the top rows, player 2's below (at 1280x720,
// y 180 +- 180 and 540 +- 180), the HUDs over them. The console renders it
// in EDRAM tiles, and each tile's window scissor keeps a view's geometry on
// its rows: Xenos clips to its guard band, not the viewport, so geometry
// reaches past a viewport's edge. Untiled rendering draws tile 0 over the
// whole target, so a draw must be cut to its own viewport, or player 1's
// geometry lands on player 2's rows (their depth too, so player 2's view
// showed only in patches).
//
// NFSMW_UNTILED_AREA (on by default on Apple only) cuts untiled draws to
// their viewports, but gave up on a second viewport in one pass: the targets
// fell back to the whole target from then on. The split-screen rules, while
// the game is in split screen (game/split_screen.h) and on every platform:
//   - CutOn: untiled draws are cut to their viewports (also where
//     NFSMW_UNTILED_AREA is off);
//   - a target's earlier fallback to the whole target (wholeArea) doesn't
//     hold (KeepsWholeArea);
//   - a draw cut to a viewport its targets' open pass doesn't cover restarts
//     the pass over both (the second view's first draw, once a frame)
//     instead of the targets falling back (FallsBack), and neither other
//     fallback is taken: two cuts whose union isn't exact, a resolve reading
//     past what the cut draws drew (pixels the console never drew either).
// Outside split screen every rule is as before. NFSMW_SPLIT_SCREEN=0: off.
#pragma once
#include <cmath>
#include <cstdint>

namespace gpu::split_views
{
    struct GuestRect
    {
        int32_t x0, y0, x1, y1;
    };

    // The guest pixels of a w x h target an untiled draw clipped to its
    // viewport touches: those whose centres lie inside the viewport. ax, ay
    // are |PA_CL_VPORT_[XY]SCALE| (y is flipped), ox, oy the offsets; the
    // window offset added (0 for tile 0) where enabled, and D3D's integer
    // pixel centres (+0.5) where they are its.
    inline GuestRect ViewportPixels(float ax, float ox, float ay, float oy, float windowX, float windowY, bool d3dCenter, int32_t w,
        int32_t h)
    {
        ox += windowX;
        oy += windowY;
        if (d3dCenter)
        {
            ox += 0.5f;
            oy += 0.5f;
        }
        auto edge = [](float v, int32_t hi) { return int32_t(std::fmax(0.0f, std::fmin(std::ceil(v - 0.5f), float(hi)))); };
        return { edge(ox - ax, w), edge(oy - ay, h), edge(ox + ax, w), edge(oy + ay, h) };
    }

    // Untiled draws cut to their viewports.
    constexpr bool CutOn(bool untiledArea, bool split)
    {
        return untiledArea || split;
    }

    // A target that fell back to the whole target keeps it.
    constexpr bool KeepsWholeArea(bool wholeArea, bool split)
    {
        return wholeArea && !split;
    }

    // A cut draw its open pass doesn't cover (or two cuts not exactly one
    // rectangle, or a resolve past the cuts) makes its targets fall back to
    // the whole target; in split screen the pass restarts instead.
    constexpr bool FallsBack(bool split)
    {
        return !split;
    }
}
