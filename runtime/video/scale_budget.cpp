// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See scale_budget.h.
#include "scale_budget.h"
#include "picture_fit.h"

namespace video
{
    namespace
    {
        // "Apple <family><generation>[ <tier>] GPU": family 'A' (iPhones, the
        // iPad mini and the base iPad) or 'M' (iPad Pro and Air), tier "Pro"
        // or empty. False for any other name.
        bool ParseAppleChip(std::string_view name, char& family, unsigned& generation, std::string_view& tier)
        {
            constexpr std::string_view kApple = "Apple ";
            if (!name.starts_with(kApple))
                return false;
            name.remove_prefix(kApple.size());
            if (name.empty() || (name[0] != 'A' && name[0] != 'M'))
                return false;
            family = name[0];
            name.remove_prefix(1);
            generation = 0;
            size_t digits = 0;
            while (digits < name.size() && name[digits] >= '0' && name[digits] <= '9' && digits < 4)
                generation = generation * 10 + unsigned(name[digits++] - '0');
            if (digits == 0 || (digits < name.size() && name[digits] != ' '))
                return false;
            name.remove_prefix(digits < name.size() ? digits + 1 : digits);
            // What's left: "GPU", or "<tier> GPU". MoltenVK names the device
            // after Metal's, which ends in " GPU"; accept a name without it too.
            constexpr std::string_view kGpu = "GPU";
            if (name.ends_with(kGpu))
                name.remove_suffix(kGpu.size());
            while (!name.empty() && name.back() == ' ')
                name.remove_suffix(1);
            tier = name;
            return true;
        }
    }

    ScaleBudget MobileScaleBudget(std::string_view gpuName)
    {
        char family = 0;
        unsigned generation = 0;
        std::string_view tier;
        if (!ParseAppleChip(gpuName, family, generation, tier))
            return { 1, "an unrecognised GPU keeps 1x1" };
        if (family == 'A' && generation == 20 && tier == "Pro")
        {
            // iPhone 18 Pro Max (2868x1320 screen), on a charger, scripted
            // fly-in and race: 60 fps at 1x1 with GPU to spare (race 7.7 ms a
            // frame), but iOS's thermal state went nominal -> fair at 75 s and
            // -> serious at 350 s (Frame Rate's Auto then dropped to 30 fps),
            // and was still serious 25 minutes later. More pixels only heat
            // it sooner: 1x1 until 2x2 is shown to stay out of serious.
            return { 1, "the A20 Pro heats to serious within 6 minutes at 1x1 on a charger" };
        }
        if (family == 'A' && generation <= 17)
        {
            // iPhone 15 Pro Max (A17 Pro), scripted race: GPU 15 ms a frame
            // at 1x1, 36-40 ms at 2x2.
            return { 1, "A17 Pro and older hold a race at 60 fps at 1x1 only" };
        }
        if (family == 'M')
        {
            // iPad Pro M2: 2x2 = 43-50 fps in a race. Newer M chips are not
            // measured in an iPad.
            return { 1, "iPads keep 1x1: the M2 makes 43-50 fps at 2x2" };
        }
        // An A-series chip newer than the A17 Pro but not measured (the
        // A18/A19, a plain A20, an A21...).
        return { 1, "a chip not measured yet keeps 1x1" };
    }

    HalfStepBudget CarbonScaleBudget(uint32_t mostWantedPixels)
    {
        // Carbon's GPU time a frame at the same scale is ~1.4 times Most
        // Wanted's (M1 Pro, 2560x1440 at 2x2: Carbon's free roam 15.8-15.9 ms
        // of GPU busy, and GPU-bound there, against Most Wanted's race 11.4
        // ms; medians of the 2 s [perf] windows), and every class keeps Most
        // Wanted's headroom: its budget over that, down to a scale the
        // renderer offers. Measured on the M1 Pro only; the discrete and Max
        // or Ultra budgets are that reasoning, not a measurement.
        if (mostWantedPixels >= 9)
            return { 24, "an M3-or-later Max or Ultra", "3x2 or 2x3",
                "Most Wanted's 3x3 is 9-11 ms of an M4 Max's GPU in a race; Carbon's ~1.4x would be 13-15 ms with peaks "
                "past 16.7 ms, so 3x3 waits for a measurement" };
        if (mostWantedPixels >= 6)
            return { 16, "a discrete GPU", "2x2", "Most Wanted's 6 host pixels per guest pixel over Carbon's ~1.4x GPU time a pixel" };
        if (mostWantedPixels >= 4)
            return { 9, "an integrated GPU", "1.5x1.5",
                "an M1 Pro in Carbon's free roam at 2560x1440 is GPU-bound at 2x2, 16-18 ms a frame and windows under 60 fps, "
                "and holds 60 fps at 1.5x1.5 with 12-14 ms" };
        return { 4, "this GPU", "1x1", "where Most Wanted keeps 1x1 (iPhones and iPads), Carbon does too" };
    }

    void AutoRenderScaleHalves(uint32_t regionW, uint32_t regionH, uint32_t quarters, uint32_t& halvesX, uint32_t& halvesY)
    {
        // The coverage alone (a budget AutoRenderScale's 3x3 cap never meets).
        uint32_t sx = 1, sy = 1;
        AutoRenderScale(regionW, regionH, 9, sx, sy);
        halvesX = 2 * sx;
        halvesY = 2 * sy;
        // The next scale down the renderer offers: 3x -> 2x -> 1.5x -> 1x
        // (2.5x isn't one).
        auto lower = [](uint32_t halves) { return halves > 4 ? 4u : halves > 2 ? halves - 1 : 2u; };
        while (halvesX * halvesY > quarters && (halvesX > 2 || halvesY > 2))
        {
            if (halvesX >= halvesY)
                halvesX = lower(halvesX);
            else
                halvesY = lower(halvesY);
        }
    }
}
