// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// HFR (game/hfr.h) on a Mac whose frames are shown at a set time after their
// guest vblank (video/present_schedule.h): how much later than that time
// both of a world step's pictures are shown, from the driver's reports.
//
// The in-between picture is shown half a vblank before the real one, so the
// real one must be ready by its time for the two to stay evenly spaced. At
// 60 fps 12.5 ms after the vblank leaves room (99.9% of frames ready by then
// while driving); with two pictures a step a heavy scene (a cutscene with ten
// cars) misses it: M2's first ProMotion run had 16.8% of its cutscene frames
// ready later than 12.5 ms, and the late real pictures, shown when ready,
// came out next to (or after) the following in-between one. So when
// kLateToGrow of the last kWindow pictures shown at a time were reported more
// than kLateNs late, both pictures of every step go a step of the panel's
// 240 Hz timing grid later (at most kMaxExtraNs), and a step earlier again
// once none has been late for `hold` (10 s at first, twice as long after each
// earlier time that turned late again within kRecentNs, up to 160 s).
// Header only: tests/hfr_schedule_test.cpp checks it.
#pragma once
#include <algorithm>
#include <cstdint>
#include <iterator>

namespace video
{
    class HfrShowAdapt
    {
    public:
        static constexpr int64_t kStepNs = 4'166'667;    // ProMotion's 240 Hz timing grid
        static constexpr int64_t kMaxExtraNs = 4 * kStepNs;
        static constexpr uint32_t kWindow = 120;           // 1 s of pictures at 120 a second
        static constexpr uint32_t kLateToGrow = 3;
        static constexpr int64_t kLateNs = 1'000'000;
        static constexpr int64_t kFirstHoldNs = 10'000'000'000;
        static constexpr int64_t kMaxHoldNs = 160'000'000'000;
        static constexpr int64_t kRecentNs = 6'000'000'000;

        // A picture shown at a time was reported shown `lateNs` after it (ns;
        // below 0: before), at `now` (ns, any steady clock). True when
        // ExtraNs changed.
        bool Report(int64_t lateNs, int64_t now)
        {
            const bool late = lateNs > kLateNs;
            if (m_count == kWindow)
                m_late -= m_window[m_next];
            else
                m_count++;
            m_window[m_next] = late;
            m_late += late;
            m_next = (m_next + 1) % kWindow;
            if (late)
                m_lastLate = now;
            if (m_late >= kLateToGrow && m_extra < kMaxExtraNs)
            {
                if (m_lastShrink && now - m_lastShrink < kRecentNs)
                    m_hold = std::min(m_hold * 2, kMaxHoldNs);
                m_extra += kStepNs;
                m_lastGrow = now;
                Clear();
                return true;
            }
            if (m_extra > 0 && now - std::max(m_lastLate, m_lastGrow) >= m_hold && now - m_lastShrink >= m_hold)
            {
                m_extra -= kStepNs;
                m_lastShrink = now;
                Clear();
                return true;
            }
            return false;
        }

        int64_t ExtraNs() const { return m_extra; }
        int64_t HoldNs() const { return m_hold; }

    private:
        void Clear()
        {
            std::fill(std::begin(m_window), std::end(m_window), false);
            m_count = m_next = m_late = 0;
        }

        bool m_window[kWindow] = {};
        uint32_t m_count = 0, m_next = 0, m_late = 0;
        int64_t m_extra = 0, m_hold = kFirstHoldNs;
        int64_t m_lastLate = 0, m_lastGrow = 0, m_lastShrink = 0;
    };
}
