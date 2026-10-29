// Tests for gpu::covers (runtime/gpu/covers.h): the NFSMW_LOG_COVERS
// detectors. ClassifyQuad on front-end quads as the renderer sees them (the
// intro's black frames filled or fading in, fades past the screen's edges,
// a layer two rows short, the HUD), FindInsetBox on scaled-down pictures (a
// dark box with a lit border round it; a black picture, a letterboxed
// movie, a dark scene, a box touching an edge), and planted thresholds that
// must fail a case each. From the repo root:
//   clang++ -std=c++20 -O2 -UNDEBUG -Iruntime tests/covers_test.cpp -o build/covers_test
//   build/covers_test
#include <cstdint>
#include <cstdio>
#include <vector>

#include "gpu/covers.h"

using namespace gpu::covers;

static int failures = 0;
#define CHECK(cond)                                                                  \
    do                                                                               \
    {                                                                                \
        if (!(cond))                                                                 \
        {                                                                            \
            fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #cond);       \
            failures++;                                                              \
        }                                                                            \
    } while (0)

namespace
{
    const Rect kPicture{ 0, 0, 1280, 720 };

    Quad MakeQuad(Rect host, uint32_t argb, float bx, float by)
    {
        Quad q;
        q.host = host;
        q.picture = kPicture;
        q.band[0] = -bx;
        q.band[1] = bx;
        q.band[2] = -by;
        q.band[3] = by;
        for (uint32_t& c : q.argb)
            c = argb;
        return q;
    }

    // A luma picture: `fill` everywhere, `box` (if any) set to `inside`.
    struct Picture
    {
        int32_t w, h;
        std::vector<uint8_t> px;
        Picture(int32_t w_, int32_t h_, uint8_t fill) : w(w_), h(h_), px(size_t(w_) * h_, fill) {}
        void Fill(Rect r, uint8_t v)
        {
            for (int32_t y = r.y0; y < r.y1; y++)
                for (int32_t x = r.x0; x < r.x1; x++)
                    px[size_t(y) * w + x] = v;
        }
        Luma L() const { return { px.data(), w, h, w }; }
    };

    void QuadCases(int& failed, const Rule& rule)
    {
        auto expect = [&](const Quad& q, Kind k) {
            if (ClassifyQuad(q, rule).kind != k)
                failed++;
        };
        // The intro's black frame (x +-0.970, y +-0.939 of the band) at 16:9:
        // a dark box all round, unless its fill took it (opaque).
        Quad frame = MakeQuad({ 19, 22, 1261, 698 }, 0xFF000000u, 0.970f, 0.939f);
        expect(frame, Kind::InsetBox);
        frame.filledOutside = true;
        expect(frame, Kind::None);
        // Fading in below the fill's F0: the border shows (a dark box).
        Quad fading = MakeQuad({ 19, 22, 1261, 698 }, 0x80000000u, 0.970f, 0.939f);
        expect(fading, Kind::InsetBox);
        // Too faint to see.
        Quad faint = MakeQuad({ 19, 22, 1261, 698 }, 0x20000000u, 0.970f, 0.939f);
        expect(faint, Kind::None);
        // A fade to black widened past the screen's edges: nothing.
        expect(MakeQuad({ -10, -10, 1290, 730 }, 0xFF000000u, 1.29f, 1.29f), Kind::None);
        // The same fade one edge short (two rows at the top): dark, short.
        expect(MakeQuad({ 0, 2, 1280, 720 }, 0xFF000000u, 1.29f, 1.29f), Kind::DarkShort);
        // A grey dim authored past the 16:9 frame but left short: a layer.
        expect(MakeQuad({ 0, 40, 1280, 680 }, 0x807F7F7Fu, 1.29f, 1.25f), Kind::LayerShort);
        // A bright panel inside the band (a menu): nothing.
        expect(MakeQuad({ 300, 200, 980, 520 }, 0xFFC0C0C0u, 0.5f, 0.4f), Kind::None);
        // A small dark box (a dialog's shadow): not most of the picture.
        expect(MakeQuad({ 400, 250, 880, 470 }, 0xFF000000u, 0.4f, 0.3f), Kind::None);
        // Dark vertex colours that vary a lot (a gradient): not near-uniform.
        Quad gradient = MakeQuad({ 19, 22, 1261, 698 }, 0xFF000000u, 0.970f, 0.939f);
        gradient.argb[2] = gradient.argb[3] = 0xFF282828u;
        expect(gradient, Kind::None);
        // A bright vertex colour over a black texture: dark.
        Quad art = MakeQuad({ 19, 22, 1261, 698 }, 0xFFFFFFFFu, 0.970f, 0.939f);
        art.texel = 0xFF080808u;
        expect(art, Kind::InsetBox);
        // Opaque (blending off) ignores the vertex alpha.
        Quad opaque = MakeQuad({ 19, 22, 1261, 698 }, 0x00000000u, 0.970f, 0.939f);
        opaque.blended = false;
        expect(opaque, Kind::InsetBox);
    }

    void PictureCases(int& failed, const BoxRule& rule)
    {
        auto expect = [&](const Picture& p, BoxKind kind) {
            const Box b = FindInsetBox(p.L(), rule);
            if (b.found != (kind != BoxKind::None) || b.kind != kind)
                failed++;
        };
        // A black box in the middle of a lit picture.
        Picture box(378, 237, 90);
        box.Fill({ 40, 30, 338, 207 }, 0);
        expect(box, BoxKind::Inset);
        // A black picture: no box.
        expect(Picture(378, 237, 0), BoxKind::None);
        // A letterboxed movie: black bars top and bottom reach the sides.
        Picture letterbox(378, 237, 90);
        letterbox.Fill({ 0, 0, 378, 25 }, 0);
        letterbox.Fill({ 0, 212, 378, 237 }, 0);
        expect(letterbox, BoxKind::None);
        // A dark scene with a lit centre: no box.
        Picture night(378, 237, 4);
        night.Fill({ 150, 90, 230, 150 }, 120);
        expect(night, BoxKind::None);
        // The report's (run r1, 3024x1898 / 8): the fade to black before the
        // last prologue race, x +-1.121 and y +-0.972 of the band, under
        // Vert+ (view scale y 0.8962) rows 15..222 of 237 black, the frozen
        // scene lit above and below.
        Picture band(378, 237, 90);
        band.Fill({ 0, 15, 378, 222 }, 0);
        expect(band, BoxKind::BandY);
        // The same beside it (Hor+ at 21:9: x +-1.121 of the band, 0.744).
        Picture pillar(428, 180, 90);
        pillar.Fill({ 36, 0, 393, 180 }, 0);
        expect(pillar, BoxKind::BandX);
        // A band that reaches the bottom: the scene above only (a bar along
        // an edge, not a layer short of both).
        Picture bottom(378, 237, 90);
        bottom.Fill({ 0, 15, 378, 237 }, 0);
        expect(bottom, BoxKind::None);
        // A small black box (a dialog): too little of the picture.
        Picture small(378, 237, 90);
        small.Fill({ 150, 90, 230, 150 }, 0);
        expect(small, BoxKind::None);
        // A box with the scene round it only on three sides (one edge on the
        // picture's): not inside it.
        Picture three(378, 237, 90);
        three.Fill({ 40, 0, 338, 207 }, 0);
        expect(three, BoxKind::None);
    }
}

int main()
{
    int failed = 0;
    QuadCases(failed, {});
    CHECK(failed == 0);
    failed = 0;
    PictureCases(failed, {});
    CHECK(failed == 0);

    // Planted thresholds: each must fail at least one case.
    {
        Rule r;
        r.visibleAlpha = 0x10;  // the faint frame counts
        int f = 0;
        QuadCases(f, r);
        CHECK(f > 0);
        r = {};
        r.minShare = 0.1f;  // the dialog's shadow counts
        f = 0;
        QuadCases(f, r);
        CHECK(f > 0);
        r = {};
        r.spread = 0x90;  // the gradient counts
        f = 0;
        QuadCases(f, r);
        CHECK(f > 0);
        r = {};
        r.slack = 4;  // the two rows don't
        f = 0;
        QuadCases(f, r);
        CHECK(f > 0);
    }
    {
        BoxRule r;
        r.ringLit = 0.0f;  // the box on the top edge counts (no border asked for, no margin)
        r.margin = 0;
        int f = 0;
        PictureCases(f, r);
        CHECK(f > 0);
        r = {};
        r.minShare = 0.05f;  // the dialog counts
        f = 0;
        PictureCases(f, r);
        CHECK(f > 0);
        r = {};
        r.darkMax = 100;  // the lit picture counts as dark: the box grows to the edges, or the night scene's centre is one
        f = 0;
        PictureCases(f, r);
        CHECK(f > 0);
    }
    if (failures)
    {
        fprintf(stderr, "covers_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("covers_test: all passed\n");
    return 0;
}
