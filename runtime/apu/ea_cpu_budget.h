// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// EA SND's CPU budget, which drops sounds when the mix takes too long
// (GitHub issue #35: one sound, usually the engine, suddenly much quieter
// until the next gear shift). Header only, so that tests/ea_cpu_budget_test
// runs the rule and checks the generated code it names.
//
// NFSMW's sound library (EA SND, statically linked) times its own mixer
// thread (sub_825E3C30) with mftb (sub_825CFEB0): each 256-sample block
// (5.33 ms) it adds up the thread's busy time between its own stamps (the
// command buffer, the voices, the DAC; mixer + 68: +4 accumulated, +8 the
// last stamp) and each voice's own time (voice + 48/52/56). Once a block,
// sub_82601528 averages the busy time over the last three blocks, adds the
// voices' costs, scales by 60 (CPU cycles per timebase tick, roughly: the
// 360's 3.2 GHz over 49.875 MHz) and compares that with the configured share
// of a block's cycles (config + 16: 90%; 3.2e9 / (48000 x 0.390625) x 90 =
// 15.36 M, 5.13 ms of timebase). Over it, sub_82601788 expels voices, the
// lowest priority first (sub_826016B8; priorities under 100 only), until it
// fits: sub_825D04B8(voice, 0) kills each one for good. Every sample sound
// NFSMW plays through EA's SND9 layer (engine layers, turbo, shifting,
// skids, wind, the other cars) is such a voice, at priority 2.55; the game
// doesn't start a killed loop again until its own logic next starts it (a
// gear change, for the engine's).
//
// On the console the mixer has a hardware thread to itself, so the budget
// only bites when the mix really is too big. Here the timebase is the host's
// clock and the mixer an ordinary host thread: a block in which the OS
// doesn't run it for a while (another thread on its core, a busy or hot Mac)
// counts as the mix taking that long. One block held up about 14 ms puts the
// three-block average over budget, and the mixer kills voices that cost
// almost nothing (the recompiled mix takes about 2% of the budget on a
// MacBook). In sandbox drag races on a MacBook it happened by itself in 3 of
// 4 races, 5 times in one (blocks "mixing" for 16-77 ms with 1-5 ms of it
// on a core); a 20 ms hold of the mixer thread
// killed every SND9 voice playing, and with the Golf GTI the mix stayed 4.5-8
// dB quieter until the next gear change.
//
// So the host never expels voices for time: sub_82601788 does nothing
// unless NFSMW_EA_CPU_BUDGET=1 (the console's rule, for tests). The budget
// check itself still runs (it only reads), and the log says when EA would
// have dropped sounds.
#pragma once
#include <cstdint>

namespace apu::ea_budget
{
    // The generated functions the rule hooks and the tests check.
    inline constexpr uint32_t kMixerThread = 0x825E3C30;   // the mixer thread's loop
    inline constexpr uint32_t kBudgetCheck = 0x82601528;   // once a block: average, compare
    inline constexpr uint32_t kExpelOverBudget = 0x82601788;  // f1 = how far over; expels until it fits
    inline constexpr uint32_t kPickVoice = 0x826016B8;     // the lowest-priority voice under 100
    inline constexpr uint32_t kExpelVoice = 0x825D04B8;    // (voice, 0): expel it
    inline constexpr uint32_t kTimebase = 0x825CFEB0;      // mftb

    // EA's units: timebase ticks x 60 (sub_82601528 at 0x826015F0-0x82601628).
    inline constexpr double kTicksPerMs = 49875.0;
    inline constexpr double kScale = 60.0;
    // The budget for `budgetPercent` (config + 16) of a block at `rate` Hz
    // with the 360's 3.2 GHz clock (mixer + 96): EA's own arithmetic.
    constexpr double Budget(double budgetPercent, double rate = 48000.0, double clock = 3.2e9)
    {
        return clock / (rate * 0.390625) * budgetPercent;
    }
    // How far over the budget a block is (> 0: EA expels voices), for the
    // mixer's own time averaged over the last three blocks (ticks) and the
    // voices' costs (ticks; each its own 3-block average). EA checks only
    // while the budget is under 100%.
    constexpr double Over(double averageTicks, double voiceTicks, double budgetPercent = 90.0)
    {
        if (budgetPercent >= 100.0)
            return 0.0;
        return (averageTicks + voiceTicks) * kScale - Budget(budgetPercent);
    }
    // In milliseconds of mixing a block (the log's unit).
    constexpr double ToMs(double eaUnits) { return eaUnits / kScale / kTicksPerMs; }

    // Whether EA's mixer may expel voices for time: only with
    // NFSMW_EA_CPU_BUDGET=1 (`env` is its value, or null).
    constexpr bool Expels(const char* env)
    {
        return env && env[0] == '1' && env[1] == '\0';
    }

    // NFS: Carbon's EA SND (a newer build of the library) keeps the same
    // budget: once a block sub_826CD9D8 (from the mixer, sub_826C6720 and
    // sub_826C6818) averages the mixer's busy time over three blocks
    // (timebase sub_826AB240, the mixer a global at 0x82FF2B78), adds the
    // voices' costs (sub_826CDAE8), scales them (the float at 0x8204EC64),
    // and over the budget (config + 212/216/220, the config at mixer + 0,
    // the share under 100%) calls sub_826CDBD0 with f1 = how far over:
    // it expels the lowest-priority voice (sub_826CDB30) by marking it
    // expelled (+67 = 2) and freeing it (sub_826AAE60) until it fits. The
    // same rule, the same hook: nothing expelled unless NFSMW_EA_CPU_BUDGET=1.
    namespace carbon
    {
        inline constexpr uint32_t kMixerUpdate = 0x826C6818;     // calls the check once a block
        inline constexpr uint32_t kBudgetCheck = 0x826CD9D8;     // average, compare
        inline constexpr uint32_t kVoiceCosts = 0x826CDAE8;      // f1 = the voices' costs, scaled
        inline constexpr uint32_t kExpelOverBudget = 0x826CDBD0;  // f1 = how far over; expels until it fits
        inline constexpr uint32_t kPickVoice = 0x826CDB30;       // the lowest-priority voice
        inline constexpr uint32_t kExpelVoice = 0x826AAE60;      // frees it
        inline constexpr uint32_t kTimebase = 0x826AB240;        // mftb
        inline constexpr uint32_t kMixer = 0x82FF2B78;           // the mixer (its config at +0)
        inline constexpr uint32_t kScale = 0x8204EC64;           // float: EA's units per tick
        inline constexpr uint32_t kBudgetFactor = 0x820C8488;    // float: in the budget's divisor
        inline constexpr uint32_t kConfigRate = 212, kConfigClock = 216, kConfigPercent = 220;
    }

    // EA's three-block average of the mixer's own busy time, as sub_82601528
    // keeps it (mixer + 80: the average, +84/+88 the last two blocks, +92
    // which one the next block replaces).
    struct Average
    {
        double history[2] = { 0, 0 };
        int next = 0;
        double value = 0;
        double Add(double blockTicks)
        {
            value = (history[0] + history[1] + blockTicks) * (1.0 / 3.0);
            history[next] = blockTicks;
            next ^= 1;
            return value;
        }
    };
}
