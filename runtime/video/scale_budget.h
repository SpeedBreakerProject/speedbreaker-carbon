// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Internal Resolution's Auto on iPhones and iPads: how many host pixels per
// guest pixel (sx x sy, video::AutoRenderScale's budget) the GPU gets, chip
// by chip. A pure function of the GPU's name, for tests/scale_budget_test.cpp.
// And NFS: Carbon's Auto (CarbonScaleBudget, AutoRenderScaleHalves; tests/
// carbon_scale_test.cpp).
//
// Only chips measured to hold a race at 60 fps with headroom, and to stay out
// of serious thermal throttling over a long session, get more than 1x1. Any
// other chip, a newer one included, keeps 1x1 until it is measured: a phone
// that renders too much heats up and throttles to 30 fps mid-session, which
// is worse than a softer picture. None has qualified yet: the A20 Pro (iPhone
// 18 Pro Max) reaches the serious thermal state at 1x1 already (on a
// charger), so it keeps 1x1 too, and Settings > Graphics > Internal
// Resolution > 2x stays the player's choice.
#pragma once
#include <cstdint>
#include <string_view>

namespace video
{
    struct ScaleBudget
    {
        // At most this many host pixels per guest pixel (1: 1x1, 4: 2x2).
        uint32_t pixels = 1;
        // Why, for the log ("[renderer] Auto internal resolution: ...").
        const char* reason = "";
    };

    // The budget for the GPU that MoltenVK names `gpuName` (Metal's device
    // name, "Apple A20 Pro GPU", "Apple M2 GPU").
    ScaleBudget MobileScaleBudget(std::string_view gpuName);

    // NFS: Carbon (SB_GAME=carbon) draws more per pixel than Most Wanted, so
    // its Auto gets a smaller budget on the same GPU, and in quarter pixels
    // so that 1.5x (1920x1080) can be Auto's pick: halvesX x halvesY, each
    // axis in half steps (2: 1x, 3: 1.5x, 4: 2x, 6: 3x), at most `quarters`
    // (9: 1.5x1.5, 16: 2x2, 24: 3x2 or 2x3). Most Wanted keeps its whole
    // scales (AutoRenderScale with its own budget) as before.
    struct HalfStepBudget
    {
        uint32_t quarters = 4;
        // For the log: the GPU's class ("an integrated GPU"), the most Auto
        // picks there ("1.5x1.5") and why ("[renderer] Auto internal
        // resolution for Carbon on <gpu>: at most <most> (<reason>)"; the
        // resolution line says "auto for Carbon on <gpu> (at most <most>)").
        const char* gpu = "";
        const char* most = "";
        const char* reason = "";
    };

    // Carbon's budget from the class Most Wanted's rule puts the GPU in
    // (its whole-pixel budget, gpu/renderer.cpp ReadRenderScale: 1 on an
    // iPhone or iPad, 4 integrated, 6 discrete, 9 an M3-or-later Max or
    // Ultra).
    HalfStepBudget CarbonScaleBudget(uint32_t mostWantedPixels);

    // Auto in half steps: video::AutoRenderScale's coverage of the
    // regionW x regionH picture (whole scales, up to 3x3), then each axis
    // stepped down a scale the renderer offers at a time (3x -> 2x -> 1.5x ->
    // 1x; the larger axis first, x on a tie, as AutoRenderScale does) until
    // halvesX x halvesY is within `quarters` (never below 1x1).
    void AutoRenderScaleHalves(uint32_t regionW, uint32_t regionH, uint32_t quarters, uint32_t& halvesX, uint32_t& halvesY);
}
