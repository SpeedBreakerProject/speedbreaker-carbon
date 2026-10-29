// Tests for gpu/split_views.h: split screen's two views in one untiled target. Each view's draws are cut to its own
// guest rows, the cuts' host rectangles at every internal resolution (1x, 1.5x, 2x, 3x, and Vert+'s taller y scale on
// screens narrower than 16:9, e.g. the MacBook Pro's 3024x1898) meet exactly, no host pixel belongs to both, and their
// union is the frame; the split-screen rules against the old fallback; renderer.cpp is read (argv[1]) to check it
// applies them. Header only; from the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/split_views_test.cpp -o build/split_views_test &&
//   build/split_views_test runtime/gpu/renderer.cpp
#include <gpu/render_scale.h>
#include <gpu/split_views.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

using namespace gpu::split_views;

// renderer.cpp's HostRect: guest edges to host edges (a negative edge is 0).
struct HostSpan { int32_t lo, hi; };
static HostSpan Host(const gpu::AxisScale& a, int32_t g0, int32_t g1)
{
    return { a.EdgeAt(g0), a.EdgeAt(g1) };
}

// The guest row (or column) whose pixel a host pixel samples for (render_scale.h's owner).
static uint32_t Owner(const gpu::AxisScale& a, uint32_t h)
{
    return gpu::ScaleGlsl{}.sbOwner(h, a.num, a.den);
}

int main(int argc, char** argv)
{
    // The registers the game sets for split screen at 1280x720 (v018 split-screen scoping, NFSMW_LOG_TILES run B3):
    // x 640 +- 640, player 1 y 180 +- 180, player 2 y 540 +- 180 (YSCALE negative: flipped), window offset 0 (tile 0).
    for (bool d3d : { true, false })
    {
        GuestRect p1 = ViewportPixels(640.0f, 640.0f, 180.0f, 180.0f, 0.0f, 0.0f, d3d, 1280, 720);
        GuestRect p2 = ViewportPixels(640.0f, 640.0f, 180.0f, 540.0f, 0.0f, 0.0f, d3d, 1280, 720);
        CHECK(p1.x0 == 0 && p1.y0 == 0 && p1.x1 == 1280 && p1.y1 == 360, "d3d %d: player 1 (%d,%d)-(%d,%d)", d3d, p1.x0, p1.y0, p1.x1, p1.y1);
        CHECK(p2.x0 == 0 && p2.y0 == 360 && p2.x1 == 1280 && p2.y1 == 720, "d3d %d: player 2 (%d,%d)-(%d,%d)", d3d, p2.x0, p2.y0, p2.x1, p2.y1);
        // A later tile's window offset (-256 rows) moves the cut with the view (those draws are skipped untiled, but
        // the rule is the host viewport's).
        GuestRect t1 = ViewportPixels(640.0f, 640.0f, 180.0f, 540.0f, 0.0f, -256.0f, d3d, 1280, 720);
        CHECK(t1.y0 == 104 && t1.y1 == 464, "d3d %d: tile 1 (%d,%d)", d3d, t1.y0, t1.y1);
    }
    // Clamped to the target (a guard band past it), and a viewport off the target cuts to nothing.
    {
        GuestRect big = ViewportPixels(700.0f, 640.0f, 400.0f, 360.0f, 0.0f, 0.0f, true, 1280, 720);
        CHECK(big.x0 == 0 && big.y0 == 0 && big.x1 == 1280 && big.y1 == 720, "clamped (%d,%d)-(%d,%d)", big.x0, big.y0, big.x1, big.y1);
        GuestRect off = ViewportPixels(100.0f, 2000.0f, 100.0f, 360.0f, 0.0f, 0.0f, true, 1280, 720);
        CHECK(off.x0 == off.x1, "off the target: %d..%d", off.x0, off.x1);
    }

    // The two cuts' host rectangles at each scale: x by y, num/den (den 1 or 2, render_scale.h). Vert+ on a screen
    // taller than 16:9 renders more host rows per guest row (AutoRenderScale scales y for narrow regions).
    const gpu::AxisScale scales[][2] = {
        { { 1, 1 }, { 1, 1 } }, { { 3, 2 }, { 3, 2 } }, { { 2, 1 }, { 2, 1 } }, { { 3, 1 }, { 3, 1 } },
        { { 3, 2 }, { 2, 1 } }, { { 2, 1 }, { 3, 1 } }, { { 1, 1 }, { 3, 2 } }, { { 2, 1 }, { 5, 2 } },
    };
    int cases = 0;
    for (auto& s : scales)
    {
        const gpu::AxisScale& sx = s[0];
        const gpu::AxisScale& sy = s[1];
        const HostSpan x = Host(sx, 0, 1280), y1 = Host(sy, 0, 360), y2 = Host(sy, 360, 720);
        CHECK(x.lo == 0 && uint32_t(x.hi) == sx.Size(1280), "%u/%u: x %d..%d", sx.num, sx.den, x.lo, x.hi);
        // They meet: player 1's last host row is right above player 2's first.
        CHECK(y1.hi == y2.lo, "%u/%u: player 1 ends at host row %d, player 2 starts at %d", sy.num, sy.den, y1.hi, y2.lo);
        // Together, the frame: the target's host rows (a 720-row target is ceil(720 num / den) host rows).
        CHECK(y1.lo == 0 && uint32_t(y2.hi) == sy.Size(720), "%u/%u: rows %d..%d of %u", sy.num, sy.den, y1.lo, y2.hi, sy.Size(720));
        // No host row of player 2's cut samples one of player 1's guest rows, nor the other way round.
        bool own = true;
        for (int32_t h = y1.lo; h < y1.hi; h++)
            own = own && Owner(sy, uint32_t(h)) < 360;
        for (int32_t h = y2.lo; h < y2.hi; h++)
            own = own && Owner(sy, uint32_t(h)) >= 360;
        CHECK(own, "%u/%u: a host row on the wrong side of the split", sy.num, sy.den);
        // Each half is half the frame, to a host row (1.5x: 540 + 540; 2.5x: 900 + 900).
        CHECK(y1.hi - y1.lo == y2.hi - y2.lo, "%u/%u: halves %d and %d host rows", sy.num, sy.den, y1.hi - y1.lo, y2.hi - y2.lo);
        cases++;
    }
    CHECK(cases == 8, "%d scale cases", cases);

    // The rules: outside split screen the old ones (NFSMW_UNTILED_AREA alone cuts; a fallback holds and is taken);
    // in split screen every untiled draw is cut and no fallback is taken or held, on every platform.
    CHECK(!CutOn(false, false) && CutOn(true, false), "outside split screen: as NFSMW_UNTILED_AREA says");
    CHECK(CutOn(false, true) && CutOn(true, true), "split screen cuts where NFSMW_UNTILED_AREA is off (Linux, Windows) too");
    CHECK(KeepsWholeArea(true, false) && !KeepsWholeArea(false, false), "outside split screen a fallback holds");
    CHECK(!KeepsWholeArea(true, true), "split screen: an earlier fallback doesn't hold");
    CHECK(FallsBack(false) && !FallsBack(true), "split screen restarts the pass instead of falling back");

    // renderer.cpp applies them: UntiledScissor's cut and its math, the uncovered draw, both other fallbacks.
    if (argc > 1)
    {
        std::ifstream f(argv[1]);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string src = ss.str();
        CHECK(!src.empty(), "read %s", argv[1]);
        auto has = [&](const char* text) { return src.find(text) != std::string::npos; };
        auto count = [&](const char* text) {
            int n = 0;
            for (size_t at = src.find(text); at != std::string::npos; at = src.find(text, at + 1))
                n++;
            return n;
        };
        CHECK(has("gpu::split_views::CutOn(UntiledAreaOn(), split)"), "UntiledScissor cuts in split screen");
        CHECK(has("gpu::split_views::KeepsWholeArea(rt->wholeArea, split)"), "UntiledScissor's fallback rule");
        CHECK(has("gpu::split_views::ViewportPixels(ax, ox, ay, oy, wx, wy, d3dCenter, w, h)"), "UntiledScissor's math is this header's");
        CHECK(!has("auto edge = [](float v, int32_t hi)"), "no second copy of the edge rule in renderer.cpp");
        CHECK(has("!Contains(s_passArea, HostRect(scale, x0, y0, x1, y1)) && !gpu::split_views::FallsBack(SplitFrame()))"),
            "Draw: the uncovered cut draw restarts its pass in split screen");
        CHECK(count("gpu::split_views::FallsBack(SplitFrame())") == 3, "the uncovered draw, the inexact union and the resolve past "
            "the cuts each ask (%d)", count("gpu::split_views::FallsBack(SplitFrame())"));
        CHECK(has("game::split_screen::GameSaysSplit()"), "the game's own predicate decides");
    }
    else
    {
        printf("  (no renderer.cpp given: its use not checked)\n");
    }

    if (g_failures)
    {
        printf("split_views_test: %d FAILED\n", g_failures);
        return 1;
    }
    printf("split_views_test: all passed\n");
    return 0;
}
