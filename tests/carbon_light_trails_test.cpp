// Tests for game::trails::carbon (runtime/game/carbon/light_trails.h): NFS:
// Carbon's nitrous light trails as its RenderFlares (sub_825AA5E0) draws them,
// with the hook and without, at 60 fps, 30 fps and frames between. From the
// repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/carbon_light_trails_test.cpp -o build/carbon_light_trails_test
//   build/carbon_light_trails_test
// The game's side is simulated as sub_825AA5E0 does it: the halfword index
// moves (0x8000 and up -> 0, then +1, wrapping at the ring's size), the hook
// runs at the matrix copy, then the newest entry is written; the trail is 8
// flares along each of three segments, entry (index + k + 1) % 3 to
// (index + k + 2) % 3 for k = 3, 2, 1, at A + (B - A) * n / 8, n = 1..8. A real
// 360 runs the race at 30 fps, so the reference is the game alone at 30 fps.
// Given game/carbon/light_trails.cpp's path (ctest passes it), it also checks
// that the hook does what this simulation does.
#include <game/carbon/light_trails.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace
{
    using namespace game::trails;
    namespace ct = game::trails::carbon;

    // A car on a curve at `speed` units per ms, its matrix tagged with the
    // time it was written (so a test can tell which frame's it is).
    Entry CarAt(double ms, double speed)
    {
        Entry e;
        e.pos[0] = float(-3000.0 + speed * ms);
        e.pos[1] = float(12.0 + 0.001 * ms);
        e.pos[2] = float(800.0 - 0.4 * speed * ms + 1e-5 * ms * ms);
        const uint64_t tag = uint64_t(std::llround(ms * 1000.0));
        memcpy(e.matrix.data(), &tag, sizeof(tag));
        return e;
    }

    uint64_t TagOf(const Entry& e)
    {
        uint64_t tag;
        memcpy(&tag, e.matrix.data(), sizeof(tag));
        return tag;
    }

    using Flare = std::array<float, 3>;

    // One car's CarRenderInfo ring as sub_825AA5E0 keeps it (at guest address
    // `info`), with the hook (as game/carbon/light_trails.cpp makes it) or not.
    struct Ring
    {
        static constexpr uint32_t kInfo = 0x4A3B2C00;
        Entry e[3];
        uint16_t index = 0;
        uint16_t size = ct::kEntries;
        History history;
        bool hooked = true;
        int filled = 0;  // ring writes the hook filled both entries in
        bool lastMiddle = false, lastOldest = false;  // what the last write filled

        // One RenderFlares call in a player view at game time `ms`.
        void Draw(double ms, const Entry& now)
        {
            if (index >= 0x8000)
                index = 0;
            index = uint16_t(index + 1);
            if (int16_t(index) >= int16_t(size))
                index = 0;
            const uint32_t slot = index;
            // The matrix copy's destination, as the game computes it.
            const uint32_t destination = kInfo + (slot + 9) * 64;
            if (hooked && ct::RingAsRead(kInfo, slot, size, destination))
            {
                history.Add(ms, now);
                const Fill f = FillFor(history, ms);
                if (f.hasMiddle)
                    e[MiddleSlot(slot)] = f.middle;
                if (f.hasOldest)
                    e[OldestSlot(slot)] = f.oldest;
                filled += f.hasMiddle && f.hasOldest;
                lastMiddle = f.hasMiddle;
                lastOldest = f.hasOldest;
            }
            if (slot < 3)
                e[slot] = now;
        }

        // The 24 flares of the trail, in the game's order.
        std::vector<Flare> Flares() const
        {
            std::vector<Flare> out;
            for (int k = int(size); k > 0; k--)
            {
                const Entry& a = e[(index + k + 1) % size];
                const Entry& b = e[(index + k + 2) % size];
                for (int n = 1; n <= 8; n++)
                {
                    const float t = float(n) * 0.125f;
                    Flare f;
                    for (int c = 0; c < 3; c++)
                        f[c] = (b.pos[c] - a.pos[c]) * t + a.pos[c];
                    out.push_back(f);
                }
            }
            return out;
        }

        // How far back the trail reaches: the newest entry to the oldest.
        double Length() const
        {
            const Entry& n = e[index];
            const Entry& o = e[OldestSlot(index)];
            double s = 0;
            for (int c = 0; c < 3; c++)
                s += (double(n.pos[c]) - double(o.pos[c])) * (double(n.pos[c]) - double(o.pos[c]));
            return std::sqrt(s);
        }
    };

    // Game time as GameFrameTime sums it: each frame's ms in 16.16, rounded.
    struct Clock
    {
        uint64_t units = 0;
        double ms = 0;
        void Advance(double frameMs)
        {
            units += uint64_t(std::llround(frameMs * 65536.0));
            ms = double(units) / 65536.0;
        }
    };

    constexpr double kSpeed = 0.05;  // units per ms: ~110 mph if a unit is a metre
    constexpr double kV = kVblankMs;

    void Layout()
    {
        printf("layout\n");
        // The ring as sub_825AA5E0 addresses it: matrices (index + 9) * 64,
        // positions (index + 48) * 16, the index at +816, the size at +818.
        CHECK(ct::kRingWriteReturn == 0x825AA7C8, "the return address after bl 0x8216DB60 at 0x825AA7C4");
        CHECK(ct::kNewPositionOnStack == 96, "the position is read from the caller's r1 + 96");
        for (uint32_t s = 0; s < 3; s++)
        {
            CHECK(ct::MatrixAt(0, s) == (s + 9) * 64, "matrix %u", s);
            CHECK(ct::PositionAt(0, s) == (s + 48) * 16, "position %u", s);
        }
        CHECK(ct::kMatrices + 3 * kMatrixBytes == ct::kPositions, "three matrices, then the positions");
        CHECK(ct::kPositions + 3 * kPositionBytes == ct::kNewestIndex, "three positions, then the index");
        CHECK(ct::kNewestIndex + 2 == ct::kRingSize, "the index halfword, then the size halfword");
        CHECK(ct::kEntries == 3, "the size the game sets (li r28,3; sth r28,818 at 0x8259FC00)");
    }

    void AsRead()
    {
        printf("ring writes as read\n");
        const uint32_t info = 0x40100000;
        for (uint32_t s = 0; s < 3; s++)
            CHECK(ct::RingAsRead(info, s, 3, info + 576 + 64 * s), "slot %u", s);
        CHECK(!ct::RingAsRead(info, 3, 3, info + 576 + 64 * 3), "index past the ring");
        CHECK(!ct::RingAsRead(info, 0xFFFF, 3, info + 576), "index not moved yet");
        CHECK(!ct::RingAsRead(info, 1, 4, info + 576 + 64), "a ring of four");
        CHECK(!ct::RingAsRead(info, 1, 2, info + 576 + 64), "a ring of two");
        CHECK(!ct::RingAsRead(info, 1, 3, info + 576), "the copy aimed at another entry");
        CHECK(!ct::RingAsRead(info, 1, 3, info + 576 + 64 + 4), "the copy aimed off the entry");
        CHECK(!ct::RingAsRead(info, 1, 3, 0x40200000 + 576 + 64), "another car's ring");
    }

    // The largest distance between two trails' flares, flare for flare.
    double MaxApart(const std::vector<Flare>& a, const std::vector<Flare>& b)
    {
        if (a.size() != b.size())
            return 1e9;
        double m = 0;
        for (size_t i = 0; i < a.size(); i++)
            for (int c = 0; c < 3; c++)
                m = std::max(m, std::abs(double(a[i][c]) - double(b[i][c])));
        return m;
    }

    // 60 fps hooked draws, at every 30 fps frame, the trail the game alone
    // draws at 30 fps (the 360's), flare for flare; unhooked it is half as
    // long. Each entry is a frame the game itself wrote (not interpolated).
    // The two clocks sum their own 16.16 frame times (2/60 s rounds once at
    // 30 fps, 1/60 s twice at 60), so they part by 1/65536 ms a console frame
    // (5 us over these 10 s): flares compared to 2e-3 units.
    void SixtyMatchesThirty()
    {
        printf("60 fps hooked = 30 fps\n");
        Ring hooked, game60, game30;
        game60.hooked = game30.hooked = false;
        Clock c60, c30;
        int compared = 0;
        for (int frame = 0; frame < 600; frame++)
        {
            c60.Advance(kV);
            hooked.Draw(c60.ms, CarAt(c60.ms, kSpeed));
            game60.Draw(c60.ms, CarAt(c60.ms, kSpeed));
            if (frame >= 9)
            {
                // Every frame: the middle entry is the frame 2 vblanks back, the oldest 4.
                const double middle = double(TagOf(hooked.e[MiddleSlot(hooked.index)])) / 1000.0;
                const double oldest = double(TagOf(hooked.e[OldestSlot(hooked.index)])) / 1000.0;
                CHECK(std::abs(c60.ms - 2 * kV - middle) < 0.01, "frame %d: middle %.4f ms back", frame, c60.ms - middle);
                CHECK(std::abs(c60.ms - 4 * kV - oldest) < 0.01, "frame %d: oldest %.4f ms back", frame, c60.ms - oldest);
            }
            if (frame % 2 == 1)
            {
                c30.Advance(2.0 * kV);
                game30.Draw(c30.ms, CarAt(c30.ms, kSpeed));
                CHECK(std::abs(c30.ms - c60.ms) < 0.01, "clocks apart at frame %d", frame);
                if (frame >= 9)
                {
                    const double apart = MaxApart(hooked.Flares(), game30.Flares());
                    CHECK(apart < 2e-3, "frame %d: the trail is %.6f units off the 360's", frame, apart);
                    const double ratio = game60.Length() / game30.Length();
                    CHECK(std::abs(ratio - 0.5) < 0.02, "frame %d: unhooked 60 fps trail %.3f of the 360's", frame, ratio);
                    compared++;
                }
            }
        }
        CHECK(compared > 250, "only %d frames compared", compared);
        CHECK(hooked.filled > 590, "the hook filled %d of 600 ring writes", hooked.filled);
    }

    // At the console's own 30 fps the hook changes nothing.
    void ThirtyUnchanged()
    {
        printf("30 fps unchanged\n");
        Ring hooked, game;
        game.hooked = false;
        Clock c;
        for (int frame = 0; frame < 300; frame++)
        {
            c.Advance(2.0 * kV);
            hooked.Draw(c.ms, CarAt(c.ms, kSpeed));
            game.Draw(c.ms, CarAt(c.ms, kSpeed));
            for (int s = 0; s < 3; s++)
                CHECK(memcmp(hooked.e[s].pos, game.e[s].pos, sizeof(game.e[s].pos)) == 0 && hooked.e[s].matrix == game.e[s].matrix,
                    "frame %d slot %d", frame, s);
        }
    }

    // The first frames of a car, and the first after a gap of more than 250
    // ms (a new car in a reused CarRenderInfo, or one back on screen): the
    // hook fills an entry only once its history reaches that far back (the
    // middle 2 vblanks, the oldest 4); until then the game's own stays.
    void FirstFramesAndGaps()
    {
        printf("first frames, gaps\n");
        Ring hooked, game;
        game.hooked = false;
        Clock c;
        for (int frame = 0; frame < 40; frame++)
        {
            c.Advance(frame == 20 ? 300.0 : kV);
            hooked.Draw(c.ms, CarAt(c.ms, kSpeed));
            game.Draw(c.ms, CarAt(c.ms, kSpeed));
            const int sinceStart = frame < 20 ? frame : frame - 20;
            CHECK(hooked.lastMiddle == (sinceStart >= 2), "frame %d: middle filled %d", frame, hooked.lastMiddle);
            CHECK(hooked.lastOldest == (sinceStart >= 4), "frame %d: oldest filled %d", frame, hooked.lastOldest);
            if (frame < 2)
                CHECK(hooked.Flares() == game.Flares(), "frame %d: the first frames differ from the game's", frame);
            if (sinceStart >= 4)
                CHECK(std::abs(hooked.Length() / game.Length() - 2.0) < 0.05, "frame %d: %.3f of the game's", frame,
                    hooked.Length() / game.Length());
        }
    }

    // Mixed frame times (whole vblanks, 1-3 a frame): the oldest entry is
    // always 4 vblanks back, the middle 2, wherever the frames fall.
    void MixedFrames()
    {
        printf("mixed frames\n");
        Ring hooked;
        Clock c;
        const int pattern[] = { 1, 1, 2, 1, 3, 1, 1, 2, 2, 1 };
        for (int frame = 0; frame < 400; frame++)
        {
            c.Advance(pattern[frame % 10] * kV);
            hooked.Draw(c.ms, CarAt(c.ms, kSpeed));
            if (frame < 8)
                continue;
            const double oldest = double(TagOf(hooked.e[OldestSlot(hooked.index)])) / 1000.0;
            const double middle = double(TagOf(hooked.e[MiddleSlot(hooked.index)])) / 1000.0;
            // Interpolated entries keep the nearer sample's matrix: within a vblank and a half.
            CHECK(std::abs(c.ms - 4 * kV - oldest) <= 1.5 * kV + 1e-3, "frame %d: oldest %.3f ms back", frame, c.ms - oldest);
            CHECK(std::abs(c.ms - 2 * kV - middle) <= 1.5 * kV + 1e-3, "frame %d: middle %.3f ms back", frame, c.ms - middle);
            const Entry& o = hooked.e[OldestSlot(hooked.index)];
            const Entry expect = CarAt(c.ms - 4 * kV, kSpeed);
            CHECK(std::abs(o.pos[0] - expect.pos[0]) < 0.05f, "frame %d: oldest x %.3f, want %.3f", frame, o.pos[0], expect.pos[0]);
        }
    }

    // The game paused (the car still, the main loop's clock running): the
    // trail collapses onto the car within 4/60 s, as the 360's does.
    void Paused()
    {
        printf("paused\n");
        Ring hooked;
        Clock c;
        for (int frame = 0; frame < 30; frame++)
        {
            c.Advance(kV);
            hooked.Draw(c.ms, CarAt(c.ms, kSpeed));
        }
        const Entry still = CarAt(c.ms, kSpeed);
        for (int frame = 0; frame < 10; frame++)
        {
            c.Advance(kV);
            hooked.Draw(c.ms, still);
            if (frame >= 4)
                CHECK(hooked.Length() == 0.0, "paused frame %d: trail %.3f", frame, hooked.Length());
        }
    }

    // A ring that isn't the game's as read (its size not 3): left alone.
    void NotAsRead()
    {
        printf("not as read\n");
        Ring hooked, game;
        game.hooked = false;
        hooked.size = game.size = 2;
        Clock c;
        for (int frame = 0; frame < 60; frame++)
        {
            c.Advance(kV);
            hooked.Draw(c.ms, CarAt(c.ms, kSpeed));
            game.Draw(c.ms, CarAt(c.ms, kSpeed));
        }
        CHECK(hooked.filled == 0, "filled %d writes of a ring of two", hooked.filled);
        for (int s = 0; s < 3; s++)
            CHECK(hooked.e[s].matrix == game.e[s].matrix, "slot %d changed", s);
        // From an index the game resets (0xFFFF, as a fresh -1): the first
        // write is slot 1, and the hook acts on it.
        Ring fresh;
        fresh.index = 0xFFFF;
        fresh.Draw(kV, CarAt(kV, kSpeed));
        CHECK(fresh.index == 1, "index %u after the first write", fresh.index);
    }
}

namespace
{
    // game/carbon/light_trails.cpp makes the ring write this simulation makes:
    // the same entries, slots, checks and clock.
    void Source(const char* path)
    {
        printf("the hook's source\n");
        std::ifstream f(path);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string src = ss.str();
        CHECK(!src.empty(), "can't read %s", path);
        for (const char* want : {
                 "PPC_FUNC(sub_8216DB60)",
                 "static_cast<uint32_t>(ctx.lr) == ct::kRingWriteReturn",
                 "const uint32_t info = ctx.r30.u32;",
                 "PPC_LOAD_U16(info + ct::kNewestIndex)",
                 "PPC_LOAD_U16(info + ct::kRingSize)",
                 "if (!ct::RingAsRead(info, newest, size, ctx.r3.u32))",
                 "ctx.r1.u32 + ct::kNewPositionOnStack",
                 "memcpy(now.matrix.data(), base + ctx.r4.u32, kMatrixBytes);",
                 "game::FrameClockMs()",
                 "h.Add(ms, now);",
                 "fill = FillFor(h, ms);",
                 "StoreEntry(base, info, MiddleSlot(newest), fill.middle);",
                 "StoreEntry(base, info, OldestSlot(newest), fill.oldest);",
                 "ct::PositionAt(info, slot)",
                 "ct::MatrixAt(info, slot)",
                 "std::getenv(\"NFSMW_LIGHT_TRAILS\")",
             })
            CHECK(src.find(want) != std::string::npos, "missing: %s", want);
        // The game's copy runs after the hook, every call.
        const size_t hook = src.find("RingWrite(ctx, base);"), copy = src.find("__imp__sub_8216DB60(ctx, base);");
        CHECK(hook != std::string::npos && copy != std::string::npos && hook < copy, "the copy must run after the hook");
    }
}

int main(int argc, char** argv)
{
    if (argc > 1)
        Source(argv[1]);
    Layout();
    AsRead();
    SixtyMatchesThirty();
    ThirtyUnchanged();
    FirstFramesAndGaps();
    MixedFrames();
    Paused();
    NotAsRead();
    if (g_failures)
    {
        printf("%d FAILURES\n", g_failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
