// SpeedBreaker. GPL-3.0-or-later (see COPYING).
//
// game/hfr_grid.h: HFR's pictures per world step on a refresh grid that
// isn't a whole multiple of 60 Hz.
#include <game/hfr_grid.h>

#include <cmath>
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
    constexpr double kStep = 1e9 / 60.0;
}

int main()
{
    float f[4];
    int64_t t[4];
    // 90 Hz, steps over 2 s at any phase: 3 pictures every 2 steps, alternating
    // 1 and 2, evenly 1/90 s apart, each step's last at or before its end.
    for (double phase : { 0.0, 1e6, 3.3e6, 5.55e6, 9e6 })
    {
        const double period = 1e9 / 90.0;
        int total = 0, ones = 0, twos = 0;
        double last = -1;
        bool even = true;
        for (int k = 0; k < 120; k++)
        {
            const double from = 1e9 + k * kStep;
            const int n = hfr::grid::Pictures(phase, period, kStep, from, f, t);
            total += n;
            ones += n == 1;
            twos += n == 2;
            for (int i = 0; i < n; i++)
            {
                if (last >= 0 && std::fabs(double(t[i]) - last - period) > 2.0)
                    even = false;
                last = double(t[i]);
                Check(f[i] > 0.0f && f[i] <= 1.0f, "90 Hz: fraction in (0, 1]");
                Check(double(t[i]) > from && double(t[i]) <= from + kStep + 1.0, "90 Hz: picture within its step");
            }
        }
        Check(total == 180, "90 Hz: 3 pictures every 2 steps");
        Check(ones == 60 && twos == 60, "90 Hz: 1 and 2 in turn");
        Check(even, "90 Hz: every refresh, once");
    }
    // 144 Hz: 2.4 a step on average (2 or 3).
    {
        int total = 0;
        for (int k = 0; k < 60; k++)
        {
            const int n = hfr::grid::Pictures(0.0, 1e9 / 144.0, kStep, 1e9 + k * kStep, f, t);
            Check(n == 2 || n == 3, "144 Hz: 2 or 3 a step");
            total += n;
        }
        Check(total == 144, "144 Hz: 144 pictures a second");
    }
    // 120 Hz aligned with the steps: 1/2 and 1 every step.
    {
        const int n = hfr::grid::Pictures(0.0, 1e9 / 120.0, kStep, 10 * kStep, f, t);
        Check(n == 2 && std::fabs(f[0] - 0.5f) < 1e-4f && std::fabs(f[1] - 1.0f) < 1e-4f, "120 Hz aligned: 1/2, 1");
    }
    // A refresh exactly at `from` belongs to the last step.
    {
        const int n = hfr::grid::Pictures(0.0, 1e9 / 90.0, kStep, 0.0, f, t);
        Check(n >= 1 && t[0] > 0, "a refresh at `from` is the last step's");
    }
    // Slower than the steps: some steps have none.
    {
        int none = 0;
        for (int k = 0; k < 60; k++)
            none += hfr::grid::Pictures(0.0, 1e9 / 50.0, kStep, 1e9 + k * kStep, f, t) == 0;
        Check(none == 10, "50 Hz: 10 steps a second without a picture");
    }
    if (s_failures)
    {
        fprintf(stderr, "hfr_grid_test: %d failures\n", s_failures);
        return 1;
    }
    printf("hfr_grid_test: all passed\n");
    return 0;
}
