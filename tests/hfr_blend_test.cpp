// SpeedBreaker. GPL-3.0-or-later (see COPYING).
//
// SPIKE (v017-hfr-spike): game/hfr_blend.h, the in-between picture's pose
// blending: world matrices (the cars' +816), view matrices (the cameras'),
// and what counts as a cut. Build by hand:
//   c++ -std=c++20 -O2 -I runtime tests/hfr_blend_test.cpp -o /tmp/hfr_blend_test && /tmp/hfr_blend_test
#include <game/hfr_blend.h>

#include <cassert>
#include <cmath>
#include <cstdio>

namespace
{
    using namespace hfr::blend;
    int s_failures = 0;
    void Check(bool ok, const char* what)
    {
        if (!ok)
        {
            fprintf(stderr, "FAIL: %s\n", what);
            s_failures++;
        }
    }
    bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

    // A world matrix: yaw about the up axis (y), a uniform scale, a position.
    void WorldOf(float yawDeg, float scale, V3 pos, float m[16])
    {
        const float c = std::cos(yawDeg / 57.2957795f), s = std::sin(yawDeg / 57.2957795f);
        const float rows[16] = { c * scale, 0, -s * scale, 0, 0, scale, 0, 0, s * scale, 0, c * scale, 0, pos.x, pos.y, pos.z, 1 };
        memcpy(m, rows, sizeof(rows));
    }
    // A view matrix for a camera at `pos` with that yaw: R = the camera's axes (as a world matrix's rows, transposed back
    // for the row-vector view convention v_cam = v R + t with t = -pos R).
    void ViewOf(float yawDeg, V3 pos, float m[16])
    {
        float w[16];
        WorldOf(yawDeg, 1.0f, { 0, 0, 0 }, w);
        // R = transpose of the camera's orientation.
        float r[9];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                r[i * 3 + j] = w[j * 4 + i];
        for (int i = 0; i < 3; i++)
        {
            for (int j = 0; j < 3; j++)
                m[i * 4 + j] = r[i * 3 + j];
            m[i * 4 + 3] = 0;
        }
        for (int j = 0; j < 3; j++)
            m[12 + j] = -(pos.x * r[0 * 3 + j] + pos.y * r[1 * 3 + j] + pos.z * r[2 * 3 + j]);
        m[15] = 1;
    }
    float Det(const float m[16])
    {
        return m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
    }
    bool Orthonormal(const float m[16], float scale = 1.0f)
    {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
            {
                const float d = Dot(Row(m, i), Row(m, j)) / (scale * scale);
                if (!Near(d, i == j ? 1.0f : 0.0f, 1e-4f))
                    return false;
            }
        return true;
    }
}

int main()
{
    float a[16], b[16], out[16], want[16];

    // The same pose blends to itself.
    WorldOf(30, 1, { 10, 2, -5 }, a);
    Check(World(a, a, 0.5f, out), "same pose blends");
    for (int i = 0; i < 16; i++)
        Check(Near(out[i], a[i]), "same pose unchanged");

    // A move only: the midpoint, the axes unchanged.
    WorldOf(30, 1, { 0, 0, 0 }, a);
    WorldOf(30, 1, { 1.5f, 0, 0 }, b);  // 200 mph for one 1/60 s step
    World(a, b, 0.5f, out);
    Check(Near(out[12], 0.75f) && Near(out[13], 0) && Near(out[14], 0), "move: midpoint");
    Check(Near(out[0], a[0]) && Near(out[10], a[10]), "move: axes kept");

    // A turn of 6 degrees: halfway is 3 degrees, orthonormal, right-handed.
    WorldOf(0, 1, { 0, 0, 0 }, a);
    WorldOf(6, 1, { 0, 0, 0 }, b);
    WorldOf(3, 1, { 0, 0, 0 }, want);
    World(a, b, 0.5f, out);
    for (int i = 0; i < 12; i++)
        Check(Near(out[i], want[i], 2e-5f), "turn: halfway angle");
    Check(Orthonormal(out), "turn: orthonormal");
    Check(Det(out) > 0, "turn: handedness kept");
    // A mirrored basis stays mirrored (no cross product).
    WorldOf(0, 1, { 0, 0, 0 }, a);
    WorldOf(6, 1, { 0, 0, 0 }, b);
    for (int k = 0; k < 3; k++)
        a[8 + k] = -a[8 + k], b[8 + k] = -b[8 + k];
    World(a, b, 0.5f, out);
    Check(Det(out) < 0, "mirrored: handedness kept");

    // A compound turn (yaw and pitch about different axes): the rows' plain blend isn't orthogonal; the result must be.
    {
        auto yawPitch = [](float yawDeg, float pitchDeg, float m[16]) {
            const float cy = std::cos(yawDeg / 57.2957795f), sy = std::sin(yawDeg / 57.2957795f);
            const float cp = std::cos(pitchDeg / 57.2957795f), sp = std::sin(pitchDeg / 57.2957795f);
            // rows of Ry * Rx
            const float rows[16] = { cy, sy * sp, -sy * cp, 0, 0, cp, sp, 0, sy, -cy * sp, cy * cp, 0, 0, 0, 0, 1 };
            memcpy(m, rows, sizeof(rows));
        };
        yawPitch(0, 0, a);
        yawPitch(40, 35, b);
        Check(World(a, b, 0.5f, out), "compound turn blends");
        Check(Orthonormal(out), "compound turn: orthonormal");
        Check(Det(out) > 0, "compound turn: handedness kept");
    }

    // A scaled model keeps its scale.
    WorldOf(0, 2.5f, { 0, 0, 0 }, a);
    WorldOf(10, 2.5f, { 0, 0, 0 }, b);
    World(a, b, 0.5f, out);
    Check(Orthonormal(out, 2.5f), "scale kept");

    // A view matrix: the camera's position at the midpoint, its axes halfway.
    ViewOf(0, { 0, 2, 0 }, a);
    ViewOf(8, { 1.5f, 2, 0.5f }, b);
    Check(View(a, b, 0.5f, out), "view blends");
    const V3 p = ViewPosition(out), pa = ViewPosition(a);
    Check(Near(pa.x, 0) && Near(pa.y, 2) && Near(pa.z, 0), "view position recovered");
    Check(Near(p.x, 0.75f, 1e-4f) && Near(p.y, 2, 1e-4f) && Near(p.z, 0.25f, 1e-4f), "view: camera at the midpoint");
    ViewOf(4, { 0.75f, 2, 0.25f }, want);
    for (int i = 0; i < 16; i++)
        Check(Near(out[i], want[i], 2e-4f), "view: the halfway camera");
    Check(Orthonormal(out), "view: orthonormal");
    // Blending the view matrices' translations directly (the naive way) is NOT the halfway camera when it turns.
    float naive = 0.0f;
    for (int i = 12; i < 15; i++)
        naive = std::max(naive, std::fabs(0.5f * (a[i] + b[i]) - want[i]));
    Check(naive > 1e-3f, "test sensitive: naive translation blend differs");

    // A cutscene camera: 1.4 km from the origin, its R a little off orthonormal (rows' lengths and dot products off by
    // ~3e-5, as a play-test's NIS cameras measured). The in-between camera sits halfway between the two
    // real cameras' actual positions (p R + t = 0), and a car 9 m ahead lands halfway between where they show it.
    {
        const V3 posA = { 1338.07f, 391.85f, 104.75f }, posB = { 1338.88f, 391.64f, 104.77f };
        auto skewed = [](float yawDeg, V3 pos, float m[16]) {
            ViewOf(yawDeg, { 0, 0, 0 }, m);
            for (int j = 0; j < 3; j++)
            {
                m[0 * 4 + j] *= 1.0f - 3e-5f;
                m[1 * 4 + j] = m[1 * 4 + j] * (1.0f - 2e-5f) + 3e-5f * m[0 * 4 + j];
                m[2 * 4 + j] = m[2 * 4 + j] * (1.0f - 4e-5f) - 2e-5f * m[1 * 4 + j];
            }
            for (int j = 0; j < 3; j++)  // t = -pos R: the camera actually at pos
                m[12 + j] = -(pos.x * m[0 * 4 + j] + pos.y * m[1 * 4 + j] + pos.z * m[2 * 4 + j]);
        };
        skewed(10.0f, posA, a);
        skewed(10.2f, posB, b);
        const V3 ea = ViewPosition(a);
        Check(Near(ea.x, posA.x, 2e-3f) && Near(ea.y, posA.y, 2e-3f) && Near(ea.z, posA.z, 2e-3f), "skewed view: actual position");
        // -t R^T (the position before the fix) is centimetres off here: the test sees the bug.
        const V3 t = Row(a, 3);
        const V3 approx = { -Dot(t, Row(a, 0)), -Dot(t, Row(a, 1)), -Dot(t, Row(a, 2)) };
        Check(Len(Sub(approx, posA)) > 0.02f, "test sensitive: -t R^T is centimetres off a skewed view");
        Check(View(a, b, 0.5f, out), "skewed view blends");
        const V3 mid = Lerp(posA, posB, 0.5f), pm = ViewPosition(out);
        Check(Len(Sub(pm, mid)) < 3e-3f, "skewed view: the in-between camera halfway");
        // A point 9 m ahead of the cameras, in each one's camera space: the in-between's halfway (to 1 mm).
        const V3 car = { posA.x + 9.0f, posA.y, posA.z + 1.0f };
        auto cam = [&](const float m[16]) {
            V3 v{};
            for (int j = 0; j < 3; j++)
                (&v.x)[j] = car.x * m[j] + car.y * m[4 + j] + car.z * m[8 + j] + m[12 + j];
            return v;
        };
        const V3 ca = cam(a), cb = cam(b), co = cam(out), cmid = Lerp(ca, cb, 0.5f);
        Check(Len(Sub(co, cmid)) < 2e-3f, "skewed view: a nearby car halfway in camera space");
    }

    // Cuts: a jump or a sharp turn isn't one step's motion.
    WorldOf(0, 1, { 0, 0, 0 }, a);
    WorldOf(0, 1, { 1.5f, 0, 0 }, b);
    Check(!WorldCut(a, b, 8, 45), "1.5 m is a step");
    WorldOf(0, 1, { 20, 0, 0 }, b);
    Check(WorldCut(a, b, 8, 45), "20 m is a cut");
    WorldOf(90, 1, { 0, 0, 0 }, b);
    Check(WorldCut(a, b, 8, 45), "90 degrees is a cut");
    ViewOf(0, { 0, 0, 0 }, a);
    ViewOf(0, { 0, 0, 30 }, b);
    Check(ViewCut(a, b, 8, 30), "camera jump is a cut");
    ViewOf(5, { 0, 0, 1 }, b);
    Check(!ViewCut(a, b, 8, 30), "camera step is not a cut");
    // A half turn can't be blended.
    WorldOf(0, 1, { 0, 0, 0 }, a);
    WorldOf(180, 1, { 0, 0, 0 }, b);
    Check(!World(a, b, 0.5f, out), "half turn refused");

    if (s_failures)
    {
        fprintf(stderr, "hfr_blend_test: %d failures\n", s_failures);
        return 1;
    }
    printf("hfr_blend_test: all passed\n");
    return 0;
}
