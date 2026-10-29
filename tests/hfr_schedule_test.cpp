// SpeedBreaker. GPL-3.0-or-later (see COPYING).
//
// video/hfr_schedule.h: HFR's show time on a Mac (both pictures of a step a
// panel step later while real pictures miss their time, earlier again after a
// quiet spell, with backoff).
#include <video/hfr_schedule.h>

#include <cstdio>

namespace
{
    int s_failures = 0;
    void Check(bool ok, const char* what)
    {
        if (!ok)
        {
            fprintf(stderr, "FAIL: %s\n", what);
            s_failures++;
        }
    }
    constexpr int64_t kMs = 1'000'000, kS = 1'000'000'000;
}

int main()
{
    using video::HfrShowAdapt;
    HfrShowAdapt a;
    int64_t now = 100 * kS;
    // On time: nothing changes.
    for (int i = 0; i < 1000; i++)
        Check(!a.Report(0, now += 8 * kMs), "on time: no change");
    Check(a.ExtraNs() == 0, "on time: no extra");
    // Two late ones in a window: still nothing; a third: a step later.
    Check(!a.Report(3 * kMs, now += 8 * kMs), "1 late");
    Check(!a.Report(3 * kMs, now += 8 * kMs), "2 late");
    Check(a.Report(3 * kMs, now += 8 * kMs), "3 late: later");
    Check(a.ExtraNs() == HfrShowAdapt::kStepNs, "a step later");
    // Late by less than kLateNs isn't late.
    for (int i = 0; i < 10; i++)
        Check(!a.Report(HfrShowAdapt::kLateNs, now += 8 * kMs), "0.99 ms: not late");
    // Late ones spread out more than a window apart don't add up.
    int64_t lastLate = 0;
    for (int k = 0; k < 5; k++)
    {
        Check(!a.Report(5 * kMs, now += 8 * kMs), "a lone late one");
        lastLate = now;
        for (int i = 0; i < 130; i++)
            a.Report(0, now += 8 * kMs);
    }
    Check(a.ExtraNs() == HfrShowAdapt::kStepNs, "lone late ones: no change");
    // 10 s without a late one: a step earlier.
    bool changed = false;
    while (!changed && now - lastLate < 20 * kS)
        changed = a.Report(0, now += 8 * kMs);
    Check(changed && a.ExtraNs() == 0, "quiet 10 s: earlier");
    Check(now - lastLate >= 10 * kS && now - lastLate <= 10 * kS + 8 * kMs, "10 s after the last late one");
    // Late again within 6 s of that: later, and the next earlier waits 20 s.
    for (int i = 0; i < 3; i++)
        a.Report(4 * kMs, now += 8 * kMs);
    Check(a.ExtraNs() == HfrShowAdapt::kStepNs, "late again: later");
    Check(a.HoldNs() == 20 * kS, "backoff doubled");
    // Never more than kMaxExtraNs.
    for (int i = 0; i < 100; i++)
        a.Report(20 * kMs, now += 8 * kMs);
    Check(a.ExtraNs() == HfrShowAdapt::kMaxExtraNs, "capped");
    if (s_failures)
    {
        fprintf(stderr, "hfr_schedule_test: %d failures\n", s_failures);
        return 1;
    }
    printf("hfr_schedule_test: all passed\n");
    return 0;
}
