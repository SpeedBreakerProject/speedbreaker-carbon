// Tests for NFS: Carbon's Internal Resolution Auto (runtime/video/
// scale_budget.cpp: CarbonScaleBudget, AutoRenderScaleHalves): its budget for
// each class of GPU Most Wanted's rule knows, the scales it picks on the
// screens that matter (1.5x1.5 on a Mac's integrated GPU at 1440p), and that
// the half-step walk is Most Wanted's own whole-scale walk whenever the budget
// is one of Most Wanted's, so Carbon differs by its budget alone. Most Wanted's
// Auto is untouched (tests/picture_fit_test.cpp, tests/scale_budget_test.cpp).
// From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/carbon_scale_test.cpp runtime/video/scale_budget.cpp runtime/video/picture_fit.cpp -o build/carbon_scale_test
//   build/carbon_scale_test
#include <video/picture_fit.h>
#include <video/scale_budget.h>

#include <cstdio>
#include <cstring>

using video::AutoRenderScale;
using video::AutoRenderScaleHalves;
using video::CarbonScaleBudget;
using video::HalfStepBudget;

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

// Most Wanted's budgets (gpu/renderer.cpp ReadRenderScale).
constexpr unsigned kMobile = 1, kIntegrated = 4, kDiscrete = 6, kMaxUltra = 9;

static bool Filled(const char* s)
{
    return s && std::strlen(s) > 0 && !std::strchr(s, '@');  // a written reason, no placeholder left
}

static void Budget(unsigned mostWanted, unsigned wantQuarters, const char* wantMost)
{
    HalfStepBudget b = CarbonScaleBudget(mostWanted);
    printf("Most Wanted's %u -> Carbon's %u quarter pixels: %s, at most %s (%s)\n", mostWanted, b.quarters, b.gpu, b.most, b.reason);
    CHECK(b.quarters == wantQuarters, "Most Wanted's %u: %u quarters, want %u", mostWanted, b.quarters, wantQuarters);
    CHECK(Filled(b.gpu) && Filled(b.most) && Filled(b.reason), "Most Wanted's %u: the log's words are missing", mostWanted);
    CHECK(std::strcmp(b.most, wantMost) == 0, "Most Wanted's %u: \"%s\", want \"%s\"", mostWanted, b.most, wantMost);
}

// "1", "1.5", "2", "3" from half steps.
static const char* Name(unsigned halves)
{
    switch (halves)
    {
    case 2: return "1";
    case 3: return "1.5";
    case 4: return "2";
    case 6: return "3";
    default: return "?";
    }
}

// The scale Carbon's Auto picks for a regionW x regionH picture on a GPU of
// Most Wanted's class `mostWanted`, wanted as half steps.
static void Carbon(unsigned mostWanted, unsigned w, unsigned h, unsigned wantX, unsigned wantY, const char* what)
{
    unsigned hx = 0, hy = 0;
    AutoRenderScaleHalves(w, h, CarbonScaleBudget(mostWanted).quarters, hx, hy);
    printf("  %-44s %4ux%-4u: %sx%s\n", what, w, h, Name(hx), Name(hy));
    CHECK(hx == wantX && hy == wantY, "%s %ux%u (Most Wanted's %u): %sx%s, want %sx%s", what, w, h, mostWanted, Name(hx), Name(hy),
        Name(wantX), Name(wantY));
}

int main()
{
    printf("Carbon's budget by Most Wanted's class of GPU:\n");
    Budget(kMobile, 4, "1x1");             // iPhones and iPads: 1x, as Most Wanted
    Budget(kIntegrated, 9, "1.5x1.5");     // M1-M5 and Pro, plain Max/Ultra before the M3, other integrated GPUs
    Budget(kDiscrete, 16, "2x2");
    Budget(kMaxUltra, 24, "3x2 or 2x3");   // M3-or-later Max and Ultra
    // Budgets Most Wanted doesn't use yet fall to the class below them.
    Budget(0, 4, "1x1");
    Budget(2, 4, "1x1");
    Budget(3, 4, "1x1");
    Budget(5, 9, "1.5x1.5");
    Budget(7, 16, "2x2");
    Budget(8, 16, "2x2");
    Budget(12, 24, "3x2 or 2x3");

    printf("An integrated GPU (a MacBook's M1 Pro):\n");
    Carbon(kIntegrated, 2560, 1440, 3, 3, "the default window on a Retina Mac");   // Most Wanted: 2x2
    Carbon(kIntegrated, 1280, 720, 2, 2, "a 1280x720 window");
    Carbon(kIntegrated, 1920, 1080, 3, 3, "1080p");                                // Most Wanted: 2x2
    Carbon(kIntegrated, 3024, 1890, 3, 3, "16:10 Mac fullscreen, Vert+");          // Most Wanted: 2x2
    Carbon(kIntegrated, 3024, 1701, 3, 3, "its 16:9 box");
    Carbon(kIntegrated, 3440, 1440, 3, 3, "ultrawide filled");                     // Most Wanted: 2x2
    Carbon(kIntegrated, 3840, 2160, 3, 3, "4K");
    Carbon(kIntegrated, 1440, 900, 3, 3, "a 1440x900 window");
    Carbon(kIntegrated, 1365, 768, 3, 3, "a 16:9 box a pixel narrower than 16:9");
    Carbon(kIntegrated, 1280, 800, 2, 2, "Steam Deck, Vert+");                     // 1x, as Most Wanted
    Carbon(kIntegrated, 1280, 960, 2, 4, "4:3 letterboxed on 1280x1024");          // 1x2, as Most Wanted
    Carbon(kIntegrated, 640, 360, 2, 2, "a small window");

    printf("A discrete GPU:\n");
    Carbon(kDiscrete, 2560, 1440, 4, 4, "1440p");
    Carbon(kDiscrete, 1920, 1080, 4, 4, "1080p");
    Carbon(kDiscrete, 3440, 1440, 4, 4, "ultrawide filled");                       // Most Wanted: 3x2
    Carbon(kDiscrete, 3840, 2160, 4, 4, "4K");                                     // Most Wanted: 2x3
    Carbon(kDiscrete, 2560, 1600, 4, 4, "16:10, Vert+");
    Carbon(kDiscrete, 1280, 720, 2, 2, "720p");

    printf("An M3-or-later Max or Ultra:\n");
    Carbon(kMaxUltra, 2560, 1440, 4, 4, "1440p");
    Carbon(kMaxUltra, 3440, 1440, 6, 4, "ultrawide filled");
    Carbon(kMaxUltra, 3840, 2160, 4, 6, "4K");                                     // Most Wanted: 3x3
    Carbon(kMaxUltra, 3456, 2234, 4, 6, "16-inch MacBook Pro fullscreen, Vert+");
    Carbon(kMaxUltra, 5120, 2880, 4, 6, "5K");

    printf("iPhones and iPads:\n");
    Carbon(kMobile, 2868, 1320, 2, 2, "iPhone 18 Pro Max");
    Carbon(kMobile, 2732, 2048, 2, 2, "iPad Pro 12.9\"");
    Carbon(kMobile, 1920, 1080, 2, 2, "1080p");

    // A budget under one host pixel per guest pixel still gives 1x1.
    for (unsigned quarters : { 0u, 1u, 3u })
    {
        unsigned hx = 0, hy = 0;
        AutoRenderScaleHalves(3840, 2160, quarters, hx, hy);
        CHECK(hx == 2 && hy == 2, "4K with %u quarters: %sx%s, want 1x1", quarters, Name(hx), Name(hy));
    }

    // The walk itself: with Most Wanted's own budgets (as quarters) it lands
    // where AutoRenderScale does, on every picture size, so Carbon's Auto is
    // Most Wanted's coverage and walk with a smaller budget, nothing else.
    printf("The half-step walk with Most Wanted's budgets:\n");
    unsigned compared = 0, differ = 0;
    for (unsigned budget : { kMobile, kIntegrated, kDiscrete, kMaxUltra })
        for (unsigned w = 320; w <= 7680; w += 40)
            for (unsigned h = 200; h <= 4320; h += 40)
            {
                unsigned sx = 0, sy = 0, hx = 0, hy = 0;
                AutoRenderScale(w, h, budget, sx, sy);
                AutoRenderScaleHalves(w, h, budget * 4, hx, hy);
                compared++;
                if (hx != 2 * sx || hy != 2 * sy)
                {
                    if (differ++ < 5)
                        CHECK(false, "%ux%u budget %u: whole %ux%u, halves %sx%s", w, h, budget, sx, sy, Name(hx), Name(hy));
                }
            }
    printf("  %u pictures x budgets, %u differ\n", compared, differ);
    CHECK(differ == 0, "%u differ", differ);

    // Every pick is a scale the renderer offers (1, 1.5, 2, 3 an axis), at
    // least 1x1, within the budget, and no axis above the coverage.
    printf("Every pick, Carbon's budgets:\n");
    unsigned picks = 0;
    for (unsigned budget : { kMobile, kIntegrated, kDiscrete, kMaxUltra })
        for (unsigned w = 320; w <= 7680; w += 40)
            for (unsigned h = 200; h <= 4320; h += 40)
            {
                const unsigned quarters = CarbonScaleBudget(budget).quarters;
                unsigned sx = 0, sy = 0, hx = 0, hy = 0;
                AutoRenderScale(w, h, 9, sx, sy);
                AutoRenderScaleHalves(w, h, quarters, hx, hy);
                picks++;
                const bool offered = (hx == 2 || hx == 3 || hx == 4 || hx == 6) && (hy == 2 || hy == 3 || hy == 4 || hy == 6);
                if (!offered || hx * hy > quarters || hx > 2 * sx || hy > 2 * sy)
                {
                    CHECK(false, "%ux%u, %u quarters: %ux%u halves (coverage %ux%u)", w, h, quarters, hx, hy, sx, sy);
                    break;
                }
            }
    printf("  %u picks checked\n", picks);

    if (g_failures)
    {
        printf("%d FAILED\n", g_failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
