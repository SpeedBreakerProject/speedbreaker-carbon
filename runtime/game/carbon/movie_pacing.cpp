// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// NFS: Carbon's movies on whole pairs of vblanks: Most Wanted's design
// (game/movie_pacing.cpp) on Carbon's newer Direct3D.
//
// The movies (29.97 fps WMVs) play through the XDK's XMV player, the same
// one as Most Wanted's: its render call (sub_827C9E58, Most Wanted's
// sub_826E8CF8) works out the time to the next frame from the player's
// audio-driven clock, sleeps that long and renders the frame, and the game
// swaps with presentation interval ONE. Carbon's D3D flips differently from
// Most Wanted's. Its swap (sub_826E3348) puts an interrupt in the ring
// between two WAIT_REG_MEMs on a word of the device's (device + 10772, +4);
// the interrupt's callback (sub_826E31B0; its argument: the frame's address
// in the high 20 bits, the interval code in bits 8-11, 1 = ONE, and an
// "immediate" threshold in percent of a vblank in the low byte) picks the
// vblank the frame goes out at: one interval after the last frame's
// (device + 16180), or the vblank under way if that has passed and less of
// it has gone than the threshold, else the next one. That target is kept at
// +16180 and, unless it is the vblank under way, queued (entries of 8 bytes
// at +16196, write index +16328); the vblank interrupt (sub_826E30B0)
// counts vblanks (+16172, its time +16176) and flips each queued frame whose
// target has come, which clears the word the command processor waits on.
// So a movie frame goes out at the first vblank after its swap that is at
// least one after the last frame's, as in Most Wanted, and the kicks'
// scatter around a vblank shows the same way: frames on 1 and 3 vblanks.
//
// The fix is Most Wanted's: a movie frame due one vblank after the last one
// (the target is last + 1, and no vblank has come since the last frame's)
// whose swap came less than the guard (10 ms) before that vblank waits for
// the next one. It is done before the callback runs, by moving its "last
// target" on by one, so the callback queues the frame for the vblank after
// (the vblank interrupt never reads +16180; only this callback does, one
// swap at a time). Only frames the XMV player rendered are held, after four
// movie frames of two vblanks or more in a row, at guest vblank rates near
// 60 Hz; game frames never. NFSMW_MOVIE_CADENCE=0 turns it off,
// NFSMW_MOVIE_CADENCE=<ms> sets the guard (default 10, at most 12). Each
// movie leaves a [movie] line: its frames by vblanks shown, and those held.
#include <stdafx.h>
#include <game/movie_pacing.h>

#include <cpu/guest_time.h>
#include <kernel/function.h>
#include <video/presenter.h>
#include <video/refresh_fit.h>
#include <video/vblank_lock.h>

extern "C" PPC_FUNC(__imp__sub_827C9E58);  // XMV player: sleep until the next frame is due, render it
extern "C" PPC_FUNC(__imp__sub_826E31B0);  // D3D: the swap's interrupt callback

namespace
{
    int64_t NowNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    const double s_guardMs = [] {
        const char* v = std::getenv("NFSMW_MOVIE_CADENCE");
        double ms = v ? std::atof(v) : 10.0;
        if (v && ms <= 0)
            fprintf(stderr, "[movie] cadence off (NFSMW_MOVIE_CADENCE=0): movie frames go out at the first vblank after their swap\n");
        return std::clamp(ms, 0.0, 12.0);
    }();

    constexpr uint32_t kSteadyFrames = 4;  // movie frames of 2+ vblanks before any hold

    // D3D's device (VdGlobalDevice, the import at 0x82000988): the vblank
    // count, the last frame's target vblank, the queue's write index.
    constexpr uint32_t kDevicePointer = 0x82000988;
    constexpr uint32_t kDeviceVblanks = 16172, kDeviceTarget = 16180, kDeviceQueueWrite = 16328;

    std::atomic<uint64_t> s_movieRenders{ 0 };  // XMV frames rendered
    std::atomic<int64_t> s_lastMovieFrame{ 0 };  // when the last one was (LastMovieFrameNs)

    // The swap callback's thread only (one swap at a time).
    struct
    {
        uint64_t renders = 0;       // s_movieRenders at the previous swap
        bool lastWasMovie = false;  // the previous swap was a movie frame
        uint32_t lastTarget = 0;    // the previous swap's target vblank
        uint32_t steady = 0;        // consecutive movie frames of 2+ vblanks
        uint64_t frames = 0, spans[5] = {}, held = 0;
    } s_state;

    void EndMovie()
    {
        auto& s = s_state;
        if (s.frames)
            fprintf(stderr, "[movie] %llu frames: %llu on 1 vblank, %llu on 2, %llu on 3, %llu on 4+; %llu held a vblank (%.1f ms guard%s)\n",
                (unsigned long long)s.frames, (unsigned long long)s.spans[1], (unsigned long long)s.spans[2],
                (unsigned long long)s.spans[3], (unsigned long long)s.spans[4], (unsigned long long)s.held, s_guardMs,
                s_guardMs > 0 ? "" : ": off");
        s.frames = s.held = 0;
        std::fill(std::begin(s.spans), std::end(s.spans), 0);
        s.steady = 0;
    }
}

PPC_FUNC(sub_827C9E58)
{
    __imp__sub_827C9E58(ctx, base);
    s_movieRenders.fetch_add(1, std::memory_order_release);
    s_lastMovieFrame.store(NowNs(), std::memory_order_relaxed);
}

int64_t game::LastMovieFrameNs()
{
    return s_lastMovieFrame.load(std::memory_order_relaxed);
}

PPC_FUNC(sub_826E31B0)
{
    auto& s = s_state;
    const uint32_t arg = ctx.r3.u32;
    const uint32_t device = PPC_LOAD_U32(PPC_LOAD_U32(kDevicePointer));
    const int64_t now = NowNs();
    const uint64_t renders = s_movieRenders.load(std::memory_order_acquire);
    const bool movie = renders != s.renders;
    s.renders = renders;

    bool hold = false;
    if (device && movie && s_guardMs > 0 && ((arg >> 8) & 0xF) == 1 && s.steady >= kSteadyFrames)
    {
        // Due one vblank after the last frame's: no vblank since it.
        const uint32_t vblanks = PPC_LOAD_U32(device + kDeviceVblanks), last = PPC_LOAD_U32(device + kDeviceTarget);
        const double period = video::GuestVblank().PeriodNs();
        hold = vblanks == last && period >= 1e9 / video::RefreshFit::kMaxHz && period <= 1e9 / video::RefreshFit::kMinHz &&
            !guesttime::Suspended() && video::LastGuestVblank() >= guesttime::LastResumeNs() &&
            double(now - video::LastGuestVblank()) >= period - s_guardMs * 1e6;
        if (hold)
            PPC_STORE_U32(device + kDeviceTarget, last + 1);
    }

    __imp__sub_826E31B0(ctx, base);
    if (!device)
        return;
    const uint32_t target = PPC_LOAD_U32(device + kDeviceTarget);
    if (hold)
        s.held++;
    // The previous frame was on screen from its target to this one's.
    if (s.lastWasMovie)
    {
        const uint32_t span = target - s.lastTarget;
        s.frames++;
        s.spans[std::min<uint32_t>(span, 4)]++;
        s.steady = span >= 2 ? s.steady + 1 : 0;
    }
    if (!movie && s.lastWasMovie)
        EndMovie();
    s.lastTarget = target;
    s.lastWasMovie = movie;
}
