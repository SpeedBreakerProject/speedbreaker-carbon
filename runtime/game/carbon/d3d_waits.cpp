// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// NFS: Carbon's Direct3D GPU waits (Most Wanted's: debug/d3d_waits.cpp).
// sub_826E0088 is D3D's GPU hang check, called once per poll by its
// busy-wait loops (10 call sites: fence, ring space, idle), with the same
// arguments as Most Wanted's: args[0] the device, args[8] the GPU counter as
// last seen, args[12] its time. The device keeps a pointer to the counter
// the GPU writes at +10768 and the last value issued at +10780 (Most
// Wanted's +10384 and +10396). Overridden here as in Most Wanted: back the
// waits off to short sleeps (cpu/gpu_wait_backoff.h, the one back-off of
// both games, Linux and macOS; without it the game's main thread spun a
// whole core in every 3D scene, and the Steam Machine's CPU sat at 85 C),
// park them while the game is suspended, and
// with NFSMW_LOG_D3D_WAIT=1 say once a second which counter they poll.
//
// sub_826DBBB0 is D3D's wait for a fence (r3 the device, r4 the fence
// value; Most Wanted's sub_82597498 takes a pointer to it): timed for the
// command processor's hitch reports ("game: ... D3D waits") and marked in
// NFSMW_TIMELINE, as debug/timeline.cpp does for Most Wanted. Carbon's
// Present isn't hooked (its D3D is a newer one), so a Carbon hitch report's
// "longest frame" and "present" stay 0.
#include <stdafx.h>
#include <kernel/function.h>
#include <cpu/guest_time.h>
#include <cpu/gpu_wait_backoff.h>
#include <debug/timeline.h>

extern "C" PPC_FUNC(__imp__sub_826E0088);
extern "C" PPC_FUNC(__imp__sub_826DBBB0);

PPC_FUNC(sub_826DBBB0)
{
    timeline::Mark(timeline::GameWaitBegin, ctx.r4.u32);
    timeline::t_waitTargetMarked = false;
    const int64_t start = guesttime::NowNs();
    __imp__sub_826DBBB0(ctx, base);
    timeline::g_gameWaitNs.fetch_add(uint64_t(guesttime::NowNs() - start), std::memory_order_relaxed);
    timeline::Mark(timeline::GameWaitEnd);
}

PPC_FUNC(sub_826E0088)
{
    guesttime::WaitWhileSuspended();
    static const bool log = std::getenv("NFSMW_LOG_D3D_WAIT") != nullptr;
    if (log)
    {
        static uint64_t polls = 0;
        static auto last = std::chrono::steady_clock::now();
        polls++;
        auto now = std::chrono::steady_clock::now();
        if (now - last > std::chrono::seconds(1))
        {
            uint32_t args = ctx.r3.u32;
            uint32_t device = PPC_LOAD_U32(args + 0);
            uint32_t counterPtr = PPC_LOAD_U32(device + 10768);
            fprintf(stderr, "[d3dwait] %llu polls/s (lr %08X) | counter at %08X = %08X, issued %08X, last seen %08X\n",
                (unsigned long long)polls, uint32_t(ctx.lr), counterPtr, PPC_LOAD_U32(counterPtr),
                PPC_LOAD_U32(device + 10780), PPC_LOAD_U32(args + 8));
            polls = 0;
            last = now;
        }
    }
    gpuwait::BackOff();
    __imp__sub_826E0088(ctx, base);
}
