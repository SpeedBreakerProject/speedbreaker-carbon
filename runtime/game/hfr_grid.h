// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// HFR (game/hfr.h) on a display at a rate that isn't a whole multiple of
// 60 Hz with a fixed refresh grid (90, 144, 165 Hz: video::HfrGrid): which
// refreshes a world step's pictures are for, and their fractions of the way
// from the last step. A step's pictures cover the refreshes after the last
// step's last picture (`from`, ns) up to a step later; each one's fraction is
// how far into the step its refresh falls. Header only:
// tests/hfr_grid_test.cpp checks it.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hfr::grid
{
    // `gridNs`: a refresh's time; `periodNs`: the refresh period; `stepNs`:
    // the world step (the guest vblank); `from`: when the last step's state
    // is shown. Up to 4 pictures, in order; 0 when none falls in the step (a
    // display slower than 60 Hz).
    inline int Pictures(double gridNs, double periodNs, double stepNs, double from, float fractions[4], int64_t targets[4])
    {
        if (!(periodNs > 0) || !(stepNs > 0))
            return 0;
        int count = 0;
        // The first refresh after `from` (a refresh exactly at it is the last step's).
        double r = gridNs + std::floor((from - gridNs) / periodNs + 1.0) * periodNs;
        if (r - from < 1.0)
            r += periodNs;
        for (; r <= from + stepNs + 1.0 && count < 4; r += periodNs)
        {
            fractions[count] = std::clamp(float((r - from) / stepNs), 0.01f, 1.0f);
            targets[count] = int64_t(std::llround(r));
            count++;
        }
        return count;
    }
}
