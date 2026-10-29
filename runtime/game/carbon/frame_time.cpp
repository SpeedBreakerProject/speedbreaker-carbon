// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See game/frame_time.h.
//
// NFS: Carbon's game time in whole vblanks: Most Wanted's rule
// (game/frame_time.cpp, game/whole_vblanks.h) on Carbon's code.
//
// Carbon times itself as Most Wanted does. Its main loop (sub_82453158)
// measures each frame with mftb, in milliseconds as 16.16 fixed point
// (`fmuls` by 65536.0 from 0x82063960, capped at 32000 ms), and hands it to
// the frame (sub_82452D78) at 0x824532A4, the same `fctiwz; stfiwx; lwz r3;
// bl` as Most Wanted's 0x823B0148. The frame turns it into seconds (x
// 1/65536 x 0.001) for its quantiser (sub_824680C8, Most Wanted's
// sub_823C6500), which keeps the frame's time at 0x82C7ABCC, and passes
// that to the world's fixed-step accumulator (sub_82529BE0; Most Wanted's
// stepper sub_823A2348 counts in steps, Carbon's in seconds): acc += dt at
// 0x82529C1C; below one step (1/60 s, 0x8205E0C8) no step runs; else
// k = (int)(acc x 59.999996) steps (0x820C86AC), at least 1 and at most 8,
// and acc -= k / 60, kept at 0 or more. The k steps go to the world as one
// message (the queue at 0x82BABAE8) that the frame then waits for. No
// interpolation: a frame's world state is its steps'.
//
// The quantiser is where Carbon parts from Most Wanted: it rounds the
// frame's seconds to whole 1/3600 s ticks (the frame's time) but keeps its
// remainder in 1/4000 s ticks (its real-time counter), so a steady 1/60 s
// frame comes out as 60, 60, 59 ticks, over and over (67, 67, 66 of the
// 4000 Hz ones: those are exact). Given 60, 60, 59 ticks, the accumulator
// runs no step in one frame in three and catches up later: measured, a
// frame without a step about every 3 s, and the world 0.56% slow (at the
// 360's 30 fps the same rounding gives 120, 120, 121 ticks: 0.28% fast).
// So:
//   - GameFrameTime (0x824532A4, r3): each frame is given whole guest
//     vblanks of 1/60 s (game::WholeVblanks, the shared rule);
//   - the quantiser (sub_824680C8, overridden, its call from the frame
//     only): after it, a frame given whole vblanks gets them as its time,
//     k x 60 ticks (0x82C64F88) and k / 60 s (0x82C7ABCC), what the world
//     and the frame's other users read; its own remainder and its 4000 Hz
//     real-time counter are left as it keeps them;
//   - WorldStepCount (0x82529C60, at `cmpwi cr6,r11,1` right after k is
//     computed; r11 = k, f0 = acc in seconds): k is rounded to the nearest
//     whole step when acc is within 0.05 of a step count from 1 to 6, as
//     Most Wanted's WorldStepTime does: float(3 / 60) x 59.999996 is
//     2.9999998, which truncates to 2 (and 6 to 5). The remainder, a hair
//     below 0, the game itself keeps at 0.
// WorldNoStep (0x82529C2C, the return below one step) counts frames the
// world ran no step in, for the [perf] line. NFSMW_GAME_TIME=0 turns it all
// off; NFSMW_GAME_TIME_LOG=<file> logs each frame (as Most Wanted's, plus
// the seconds the frame handed the world).
#include <stdafx.h>
#include <game/frame_time.h>
#include <game/whole_vblanks.h>

#include <kernel/function.h>
#include <video/presenter.h>
#include <video/refresh_fit.h>
#include <video/vblank_lock.h>

extern "C" PPC_FUNC(__imp__sub_82529BE0);
extern "C" PPC_FUNC(__imp__sub_824680C8);

namespace
{
    const bool s_enabled = [] {
        const char* v = std::getenv("NFSMW_GAME_TIME");
        bool on = !v || v[0] != '0';
        if (!on)
            fprintf(stderr, "[game] game time as measured (NFSMW_GAME_TIME=0)\n");
        return on;
    }();
    std::atomic<uint64_t> s_frames{ 0 }, s_snapped{ 0 }, s_noStep{ 0 }, s_multiStep{ 0 };
    std::atomic<uint64_t> s_clock{ 0 };  // game time handed to the frames (ms, 16.16)
    // NFSMW_GAME_TIME_LOG=<file>: one line per frame, written when the next
    // one starts: its measured ms, guest vblanks, whether it was given whole
    // vblanks, the world steps it ran (-1: no world; 0: below one step), how
    // long after the last vblank it started (us), the guest vblanks it was
    // given (0: its measured time), and the seconds it handed the world.
    FILE* const s_log = [] {
        const char* path = std::getenv("NFSMW_GAME_TIME_LOG");
        FILE* f = path ? fopen(path, "w") : nullptr;
        if (f)
            fprintf(f, "measured_ms,vblanks,snapped,steps,after_vblank_us,given,world_dt_ms\n");
        return f;
    }();
    struct LogLine
    {
        double measuredMs = 0, afterVblankUs = 0, worldDtMs = -1;
        uint64_t vblanks = 0, given = 0;
        int steps = -1;
        bool snapped = false, pending = false;
    } s_line;  // main thread only
    game::WholeVblanks s_whole;  // main thread only
    // The 1/60 s vblanks the frame under way was given (0: its measured
    // time), for the quantiser's override. Main thread only.
    uint64_t s_givenSixtieths = 0;
    // The quantiser's call in the frame (sub_82452D78): its return address.
    constexpr uint32_t kQuantiserCall = 0x82452DC4;
    constexpr uint32_t kFrameTicks = 0x82C64F88;    // the frame's 1/3600 s ticks
    constexpr uint32_t kFrameSeconds = 0x82C7ABCC;  // the frame's time, s (float)

    bool NearSixty(double vblankMs)
    {
        return vblankMs >= 1000.0 / video::RefreshFit::kMaxHz && vblankMs <= 1000.0 / video::RefreshFit::kMinHz;
    }
}

namespace game
{
    FrameTimeStats GetFrameTimeStats()
    {
        return { s_frames.load(std::memory_order_relaxed), s_noStep.load(std::memory_order_relaxed),
            s_multiStep.load(std::memory_order_relaxed), s_snapped.load(std::memory_order_relaxed) };
    }

    double FrameClockMs()
    {
        return double(s_clock.load(std::memory_order_relaxed)) / 65536.0;
    }
}

// GameFrameTime, before `bl 0x82452D78` at 0x824532A4 in the main loop: r3
// is the frame's measured time, milliseconds in 16.16 fixed point.
void GameFrameTime(PPCRegister& r3)
{
    struct AddToClock
    {
        PPCRegister& r3;
        ~AddToClock() { s_clock.fetch_add(uint64_t(std::max(int32_t(r3.u32), 0)), std::memory_order_relaxed); }
    } addToClock{ r3 };
    static uint64_t last = 0;
    uint64_t now = video::GuestVblanks(), vblanks = now - last;
    last = now;
    s_frames.fetch_add(1, std::memory_order_relaxed);
    s_givenSixtieths = 0;
    double measuredMs = double(int32_t(r3.u32)) / 65536.0;
    if (s_log)
    {
        if (s_line.pending)
            fprintf(s_log, "%.3f,%llu,%d,%d,%.0f,%llu,%.3f\n", s_line.measuredMs, (unsigned long long)s_line.vblanks, s_line.snapped,
                s_line.steps, s_line.afterVblankUs, (unsigned long long)s_line.given, s_line.worldDtMs);
        int64_t since = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() -
            video::LastGuestVblank();
        s_line = { measuredMs, double(since) / 1e3, -1, vblanks, 0, -1, false, true };
    }
    if (!s_enabled)
        return;
    double refreshMs = video::GuestVblank().PeriodNs() / 1e6;
    if (!NearSixty(refreshMs))
    {
        s_whole.Reset();
        return;
    }
    int divider = video::GuestVblankDivider();
    uint64_t given = s_whole.Give(measuredMs, vblanks, refreshMs * divider);
    if (!given)
        return;
    r3.u64 = uint32_t(std::llround(double(given * divider) * (1000.0 / 60.0) * 65536.0));
    s_givenSixtieths = given * divider;
    s_snapped.fetch_add(1, std::memory_order_relaxed);
    s_line.snapped = true;
    s_line.given = given;
}

// The quantiser, sub_824680C8 (f1: the frame's seconds): a frame given whole
// vblanks gets exactly them as its 1/3600 s ticks and its time.
PPC_FUNC(sub_824680C8)
{
    const uint32_t lr = uint32_t(ctx.lr);
    __imp__sub_824680C8(ctx, base);
    if (lr != kQuantiserCall || !s_givenSixtieths || s_givenSixtieths > 6)
        return;
    PPC_STORE_U32(kFrameTicks, uint32_t(s_givenSixtieths * 60));
    const float seconds = float(double(s_givenSixtieths) / 60.0);
    uint32_t bits;
    memcpy(&bits, &seconds, sizeof(bits));
    PPC_STORE_U32(kFrameSeconds, bits);
}

// The world's accumulator, sub_82529BE0 (f1: the frame's seconds): wrapped
// only to log what the frame handed it.
PPC_FUNC(sub_82529BE0)
{
    if (s_log)
        s_line.worldDtMs = ctx.f1.f64 * 1000.0;
    __imp__sub_82529BE0(ctx, base);
}

// WorldStepCount, at `cmpwi cr6,r11,1` (0x82529C60): r11 = the steps
// truncated from f0, the accumulated seconds (one step or more).
void WorldStepCount(PPCRegister& r11, PPCRegister& f0)
{
    if (s_enabled && NearSixty(video::GuestVblank().PeriodNs() / 1e6))
    {
        double steps = f0.f64 * 60.0, k = std::round(steps);
        if (k >= 1 && k <= 6 && std::abs(steps - k) <= 0.05)
            r11.u64 = uint32_t(int32_t(k));
    }
    int32_t k = std::clamp(int32_t(r11.u32), 1, 8);  // the game's own bounds, applied next
    s_line.steps = k;
    if (k >= 2)
        s_multiStep.fetch_add(1, std::memory_order_relaxed);
}

// WorldNoStep, at the return below one step (0x82529C2C).
void WorldNoStep()
{
    s_line.steps = 0;
    s_noStep.fetch_add(1, std::memory_order_relaxed);
}
