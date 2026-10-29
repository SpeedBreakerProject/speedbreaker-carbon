// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// SPIKE (v017-hfr-spike, path A of the 120 fps study): blending two poses of
// the game's 4x4 matrices for an in-between picture. Apart from game/hfr.cpp
// so tests/hfr_blend_test.cpp can check it.
//
// NFSMW's matrices are row-major and act on row vectors (v' = v M): rows 0-2
// are the basis, row 3 the translation, column 3 (0, 0, 0, 1).
//   - A world matrix (a car's at its render object +816): rows 0-2 its axes
//     in the world, row 3 its position.
//   - A camera's (camera object +0, what the view setup sub_8243EAC0 reads)
//     is a view matrix, world -> camera: v_cam = v_world R + t, so the
//     camera's position p solves p R + t = 0: p = -t R^-1 (-t R^T only
//     when R is orthonormal, which a cutscene camera's isn't quite: see
//     ViewPosition). (Measured: its p sits a few metres from the player's car.)
//     The blended view's translation is made from its blended axes and the
//     blended position, so it puts the camera exactly there.
// Axes are blended and made orthonormal again (Gram-Schmidt in row order,
// which keeps the handedness: no cross product), each row's length blended
// (a scaled model keeps its scale); positions are blended linearly. For the
// small turns between two 1/60 s steps this is as good as a slerp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>

namespace hfr::blend
{
    struct V3
    {
        float x, y, z;
    };
    inline V3 Row(const float m[16], int r) { return { m[r * 4], m[r * 4 + 1], m[r * 4 + 2] }; }
    inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline float Len(V3 a) { return std::sqrt(Dot(a, a)); }
    inline V3 Lerp(V3 a, V3 b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t }; }
    inline V3 Sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline V3 Scale(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }

    // The axes of a and b blended at t, orthonormal, then scaled to their
    // blended lengths. False when they collapse (a half turn: not blendable).
    inline bool Axes(const float a[16], const float b[16], float t, V3 out[3])
    {
        for (int r = 0; r < 3; r++)
        {
            V3 v = Lerp(Row(a, r), Row(b, r), t);
            for (int q = 0; q < r; q++)
                v = Sub(v, Scale(out[q], Dot(v, out[q])));
            const float l = Len(v);
            if (!(l > 1e-6f))
                return false;
            out[r] = Scale(v, 1.0f / l);
        }
        for (int r = 0; r < 3; r++)
        {
            const float la = Len(Row(a, r)), lb = Len(Row(b, r));
            out[r] = Scale(out[r], la + (lb - la) * t);
        }
        return true;
    }

    inline void Put(float m[16], int r, V3 v, float w)
    {
        m[r * 4] = v.x;
        m[r * 4 + 1] = v.y;
        m[r * 4 + 2] = v.z;
        m[r * 4 + 3] = w;
    }

    // A world matrix between a (t = 0) and b (t = 1). False: not blendable.
    inline bool World(const float a[16], const float b[16], float t, float out[16])
    {
        V3 axes[3];
        if (!Axes(a, b, t, axes))
            return false;
        for (int r = 0; r < 3; r++)
            Put(out, r, axes[r], a[r * 4 + 3] + (b[r * 4 + 3] - a[r * 4 + 3]) * t);
        Put(out, 3, Lerp(Row(a, 3), Row(b, 3), t), a[15] + (b[15] - a[15]) * t);
        return true;
    }

    // A view matrix's camera position: the point p that p R + t takes to the
    // origin, p = -t R^-1 (in doubles). Not -t R^T: the game's cutscene (NIS)
    // cameras aren't quite orthonormal (rows' lengths and dot products off by
    // 2-7e-5), and 1.4 km from the world's origin R^T for R^-1 put them 4-10
    // cm off. The in-between picture's camera went that far from halfway, and
    // a car close to a long lens jumped 2-37 px (at 1280 px wide) in every
    // in-between picture: the ghost seen in a play-test (2026-10-18,
    // the pre-race pan, measured). A singular R: -t R^T.
    inline V3 ViewPosition(const float m[16])
    {
        const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], k = m[10];
        // The adjugate's rows (inverse = adjugate / determinant).
        const double i00 = e * k - f * h, i01 = c * h - b * k, i02 = b * f - c * e;
        const double i10 = f * g - d * k, i11 = a * k - c * g, i12 = c * d - a * f;
        const double i20 = d * h - e * g, i21 = b * g - a * h, i22 = a * e - b * d;
        const double det = a * i00 + b * i10 + c * i20;
        const double t0 = m[12], t1 = m[13], t2 = m[14];
        if (!(std::fabs(det) > 1e-12))
        {
            const V3 t = Row(m, 3);
            return { -Dot(t, Row(m, 0)), -Dot(t, Row(m, 1)), -Dot(t, Row(m, 2)) };
        }
        return { float(-(t0 * i00 + t1 * i10 + t2 * i20) / det), float(-(t0 * i01 + t1 * i11 + t2 * i21) / det),
            float(-(t0 * i02 + t1 * i12 + t2 * i22) / det) };
    }

    // A view matrix between a (t = 0) and b (t = 1): the camera's position and
    // axes blended, the translation made from them. False: not blendable.
    inline bool View(const float a[16], const float b[16], float t, float out[16])
    {
        V3 axes[3];
        if (!Axes(a, b, t, axes))
            return false;
        const V3 p = Lerp(ViewPosition(a), ViewPosition(b), t);
        for (int r = 0; r < 3; r++)
            Put(out, r, axes[r], a[r * 4 + 3] + (b[r * 4 + 3] - a[r * 4 + 3]) * t);
        // t_j = -sum_i p_i R_ij (R's rows are axes[i]).
        const V3 tr = { -(p.x * axes[0].x + p.y * axes[1].x + p.z * axes[2].x), -(p.x * axes[0].y + p.y * axes[1].y + p.z * axes[2].y),
            -(p.x * axes[0].z + p.y * axes[1].z + p.z * axes[2].z) };
        Put(out, 3, tr, a[15] + (b[15] - a[15]) * t);
        return true;
    }

    // The largest turn of any axis between a and b, in degrees.
    inline float TurnDegrees(const float a[16], const float b[16])
    {
        float worst = 0.0f;
        for (int r = 0; r < 3; r++)
        {
            const V3 x = Row(a, r), y = Row(b, r);
            const float lx = Len(x), ly = Len(y);
            if (!(lx > 1e-6f) || !(ly > 1e-6f))
                return 180.0f;
            const float c = std::clamp(Dot(x, y) / (lx * ly), -1.0f, 1.0f);
            worst = std::max(worst, std::acos(c) * 57.2957795f);
        }
        return worst;
    }

    // Too far apart to be one step's motion: a cut, a reset, a teleport.
    inline bool WorldCut(const float a[16], const float b[16], float maxMove, float maxTurnDegrees)
    {
        return Len(Sub(Row(a, 3), Row(b, 3))) > maxMove || TurnDegrees(a, b) > maxTurnDegrees;
    }
    inline bool ViewCut(const float a[16], const float b[16], float maxMove, float maxTurnDegrees)
    {
        return Len(Sub(ViewPosition(a), ViewPosition(b))) > maxMove || TurnDegrees(a, b) > maxTurnDegrees;
    }
}
