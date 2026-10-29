// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Direct3D's GPU waits back off to short sleeps: each game's D3D busy-waits
// for the command processor (fence, ring space, idle) and calls its GPU hang
// check once per poll; that hang check's hook calls BackOff() first (Most
// Wanted: debug/d3d_waits.cpp, sub_825A5B20; NFS: Carbon:
// game/carbon/d3d_waits.cpp, sub_826E0088). One back-off for both games.
//
// On the 360 a spinning hardware thread cost nothing. Here the game's main
// thread spun a whole core at 100% while it waited for the command
// processor, and on the Steam Machine's CPU (two fast cores, four slow ones:
// see hostcpu::ReserveFastCore) the scheduler kept the spinning thread on a
// fast core. A wait still spins for its first 20 us, so short waits end on
// time; after that each poll sleeps 50 us through cpu/backoff.h, which times
// each sleep (macOS's stretched timers: a sleep 2 ms late makes the thread
// yield for a second) and counts the polls for NFSMW_BACKOFF_LOG=1. Linux and
// Apple (macOS since v0.2.0, from Simonxdd's PR #48); Windows spins, as
// backoff::Enabled says. NFSMW_GPU_WAIT_BACKOFF=0 spins throughout.
//
// (Carbon's hook lacked this until 2026-10-19: on the Steam Machine its main
// thread spun a core in every 3D scene and the CPU sat at 85 C, where Most
// Wanted's races run at 50-60 C.)
#pragma once
#include <chrono>

#include "backoff.h"

namespace gpuwait
{
    using Clock = std::chrono::steady_clock;

    // One wait's polls are at most ~60 us apart: a longer gap starts a new wait.
    inline constexpr auto kNewWaitGap = std::chrono::microseconds(200);
    inline constexpr auto kSpinFor = std::chrono::microseconds(20);
    inline constexpr auto kSleep = std::chrono::microseconds(50);

    struct BackOffState
    {
        Clock::time_point lastPoll{}, waitStart{};
    };

    // A poll at `now`: whether it sleeps (the wait has spun kSpinFor already).
    inline bool ShouldSleep(BackOffState& s, Clock::time_point now)
    {
        if (now - s.lastPoll > kNewWaitGap)
            s.waitStart = now;
        s.lastPoll = now;
        return now - s.waitStart >= kSpinFor;
    }

    // Called once per poll, before the game's hang check.
    inline void BackOff()
    {
        backoff::CountGpuWaitPoll();
        if (!backoff::Enabled(backoff::kGpuWait))
            return;
        thread_local BackOffState s;
        if (!ShouldSleep(s, Clock::now()))
            return;
        backoff::Sleep(backoff::kGpuWait, kSleep);
        s.lastPoll = Clock::now();
    }
}
