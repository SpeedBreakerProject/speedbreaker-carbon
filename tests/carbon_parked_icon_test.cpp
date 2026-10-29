// NFS: Carbon's pulsing minimap icon in the strip below the HUD under Vert+
// (gpu/edge_bars.h Parked). In free roam Carbon keeps the minimap's
// hiding-spot icon (MINIMAP_ICON_HIDING_SPOT over DEFAULTALPHA) parked below
// the 640x480 front end, pulsing. On a 16:10 screen Vert+ shows the rows below
// the 16:9 band, and the band clip let the icon through whenever its pulse
// scaled it up across the band's edge: a ring of arrows at ~31% across, at the
// bottom, all the time (operator, Steam Deck and Mac, 2026-10-20..23). The
// extents below are the icon's draws as logged on the Mac at 1280x800
// (front-end y scale 0.828, band +-0.9). Also reads the two games' answers to
// game::FrontEndParksByCentre and the renderer's use of the rule.
//   clang++ -std=c++20 -O2 -Iruntime tests/carbon_parked_icon_test.cpp -o build/carbon_parked_icon_test
//   build/carbon_parked_icon_test runtime/game/carbon/ultrawide.cpp runtime/game/ultrawide.cpp runtime/gpu/renderer.cpp
#include <gpu/edge_bars.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace
{
    using gpu::bars::Parked;
    constexpr float kLo = -0.9f, kHi = 0.9f;  // 16:10: the band is clip y +-0.9

    void TestHidingSpotIcon()
    {
        // At rest (the DEFAULTALPHA glow and the icon): wholly below the band.
        CHECK(Parked(-0.969f, -0.914f, kLo, kHi, true), "Carbon: the icon at rest is parked");
        CHECK(Parked(-0.969f, -0.914f, kLo, kHi, false), "Most Wanted's rule: so is a quad wholly outside");
        // At the pulse's peak: -1.018..-0.866, 0.034 of its 0.152 inside the band.
        CHECK(Parked(-1.018f, -0.866f, kLo, kHi, true), "Carbon: the icon at its pulse's peak is parked (the bug: it was drawn whole)");
        CHECK(!Parked(-1.018f, -0.866f, kLo, kHi, false), "Most Wanted's rule unchanged: it meets the band");
        // The same past the top edge.
        CHECK(Parked(0.866f, 1.018f, kLo, kHi, true), "Carbon: centred past the top edge is parked");
    }

    void TestLayersStillMeetTheBand()
    {
        for (bool byCentre : { false, true })
        {
            // A fade or dim authored to +-1.29 covers the band.
            CHECK(!Parked(-1.29f, 1.29f, kLo, kHi, byCentre), "a full-screen fade meets the band (byCentre %d)", byCentre);
            // A bar from inside the band to a little past its edge (the vid cam's).
            CHECK(!Parked(-0.95f, -0.2f, kLo, kHi, byCentre), "a bar reaching past the edge meets the band (byCentre %d)", byCentre);
            CHECK(!Parked(-0.93f, 0.93f, kLo, kHi, byCentre), "a panel just past both edges meets the band (byCentre %d)", byCentre);
            // The HUD inside the band (the minimap itself, the speedometer).
            CHECK(!Parked(-0.86f, -0.5f, kLo, kHi, byCentre), "the minimap is inside (byCentre %d)", byCentre);
            // Exactly on the edge: the centre on it is inside.
            CHECK(!Parked(-0.95f, -0.85f, kLo, kHi, byCentre), "centred on the band's edge is inside (byCentre %d)", byCentre);
        }
    }

    std::string Read(const char* path)
    {
        std::ifstream f(path);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    // The body of `bool FrontEndParksByCentre()` returns `want`.
    void CheckAnswer(const char* path, const char* want)
    {
        const std::string s = Read(path);
        CHECK(!s.empty(), "%s read", path);
        const size_t at = s.find("bool FrontEndParksByCentre()");
        CHECK(at != std::string::npos, "%s defines FrontEndParksByCentre", path);
        if (at == std::string::npos)
            return;
        const size_t end = s.find('}', at);
        const std::string body = s.substr(at, end - at);
        CHECK(body.find(std::string("return ") + want + ";") != std::string::npos, "%s: FrontEndParksByCentre returns %s", path, want);
    }
}

int main(int argc, char** argv)
{
    TestHidingSpotIcon();
    TestLayersStillMeetTheBand();
    if (argc >= 4)
    {
        CheckAnswer(argv[1], "true");   // game/carbon/ultrawide.cpp
        CheckAnswer(argv[2], "false");  // game/ultrawide.cpp (Most Wanted)
        const std::string r = Read(argv[3]);
        CHECK(r.find("gpu::bars::Parked(loY, hiY, bandLo, bandHi, parkByCentre)") != std::string::npos,
            "renderer.cpp's band clip uses Parked");
        CHECK(r.find("parkByCentre = game::FrontEndParksByCentre()") != std::string::npos, "renderer.cpp asks the game");
        CHECK(r.find("(hiY < bandLo || loY > bandHi)") == std::string::npos, "renderer.cpp no longer has its own wholly-outside test");
    }
    else
    {
        printf("  FAIL: pass game/carbon/ultrawide.cpp, game/ultrawide.cpp and gpu/renderer.cpp\n");
        g_failures++;
    }
    if (g_failures)
    {
        printf("carbon_parked_icon_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("carbon_parked_icon_test: all passed\n");
    return 0;
}
