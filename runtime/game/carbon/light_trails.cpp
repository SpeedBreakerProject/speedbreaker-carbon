// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See game/carbon/light_trails.h.
//
// NFS: Carbon's nitrous light trails at the 360's length: Most Wanted's
// game/light_trails.cpp on Carbon's code. One function override (the
// generated functions are weak; no ppc-carbon/ regeneration): sub_8216DB60,
// the game's 4x4 matrix copy (r3 = destination, r4 = source), called from many
// places. Called from RenderFlares' ring write (ctx.lr = kRingWriteReturn),
// it first records the car's new entry and fills the ring's other two entries
// with the car 2/60 and 4/60 s of game time ago; then the copy runs as always.
// NFSMW_LIGHT_TRAILS=0 leaves the ring as the game writes it (an A/B switch:
// trails half the 360's length at 60 fps).
#include <stdafx.h>
#include "light_trails.h"
#include <game/frame_time.h>

#include <kernel/function.h>

#include <mutex>
#include <unordered_map>

extern "C" PPC_FUNC(__imp__sub_8216DB60);

namespace
{
    using namespace game::trails;
    namespace ct = game::trails::carbon;

    const bool s_enabled = [] {
        const char* v = std::getenv("NFSMW_LIGHT_TRAILS");
        const bool on = !(v && v[0] == '0');
        if (!on)
            fprintf(stderr, "[trails] NFSMW_LIGHT_TRAILS=0: nitrous light trails as the game draws them at this frame rate "
                            "(the last two frames: half the 360's length at 60 fps)\n");
        return on;
    }();

    std::mutex s_mutex;
    // Per CarRenderInfo (guest address).
    std::unordered_map<uint32_t, History> s_cars;
    double s_lastPruneMs = 0.0;

    float LoadFloat(uint8_t* base, uint32_t address)
    {
        const uint32_t bits = PPC_LOAD_U32(address);
        float f;
        memcpy(&f, &bits, sizeof(f));
        return f;
    }

    void StoreFloat(uint8_t* base, uint32_t address, float f)
    {
        uint32_t bits;
        memcpy(&bits, &f, sizeof(bits));
        PPC_STORE_U32(address, bits);
    }

    void StoreEntry(uint8_t* base, uint32_t info, uint32_t slot, const Entry& e)
    {
        const uint32_t pos = ct::PositionAt(info, slot);
        for (uint32_t k = 0; k < 3; k++)
            StoreFloat(base, pos + k * 4, e.pos[k]);
        memcpy(base + ct::MatrixAt(info, slot), e.matrix.data(), kMatrixBytes);
    }

    void RingWrite(PPCContext& ctx, uint8_t* base)
    {
        const uint32_t info = ctx.r30.u32;
        const uint32_t newest = PPC_LOAD_U16(info + ct::kNewestIndex);
        const uint32_t size = PPC_LOAD_U16(info + ct::kRingSize);
        // The game's code as read (sub_825AA5E0): anything else, leave it be.
        if (!ct::RingAsRead(info, newest, size, ctx.r3.u32))
        {
            static std::once_flag once;
            std::call_once(once, [&] {
                fprintf(stderr, "[trails] unexpected ring write (info %08X index %u size %u destination %08X): trails left as the game draws them\n",
                    info, newest, size, ctx.r3.u32);
            });
            return;
        }
        Entry now;
        const uint32_t stack = ctx.r1.u32 + ct::kNewPositionOnStack;
        for (uint32_t k = 0; k < 3; k++)
            now.pos[k] = LoadFloat(base, stack + k * 4);
        memcpy(now.matrix.data(), base + ctx.r4.u32, kMatrixBytes);

        const double ms = game::FrameClockMs();
        Fill fill;
        {
            std::lock_guard lock(s_mutex);
            // Cars gone for a second are forgotten (CarRenderInfos are freed
            // and reused).
            if (ms - s_lastPruneMs > 1000.0 || ms < s_lastPruneMs)
            {
                std::erase_if(s_cars, [&](const auto& kv) { return ms - kv.second.NewestMs() > 1000.0; });
                s_lastPruneMs = ms;
            }
            History& h = s_cars[info];
            h.Add(ms, now);
            fill = FillFor(h, ms);
        }
        if (fill.hasMiddle)
            StoreEntry(base, info, MiddleSlot(newest), fill.middle);
        if (fill.hasOldest)
            StoreEntry(base, info, OldestSlot(newest), fill.oldest);
        if (fill.hasMiddle && fill.hasOldest)
        {
            static std::once_flag once;
            std::call_once(once, [] {
                fprintf(stderr, "[trails] nitrous light trails at the 360's length: the car's ring holds it now and %.1f and %.1f ms "
                                "of game time ago, as at the console's 30 fps (NFSMW_LIGHT_TRAILS=0: the game's last two frames)\n",
                    kConsoleFrameMs, 2.0 * kConsoleFrameMs);
            });
        }
    }
}

PPC_FUNC(sub_8216DB60)
{
    if (s_enabled && static_cast<uint32_t>(ctx.lr) == ct::kRingWriteReturn)
        RingWrite(ctx, base);
    __imp__sub_8216DB60(ctx, base);
}
