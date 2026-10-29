// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The guest's critical sections (RtlEnterCriticalSection and friends, in
// imports_unleashed.cpp), on the guest's own XRTL_CRITICAL_SECTION: a 16-byte
// dispatcher header, LockCount, RecursionCount, OwningThread.
//
// RecursionCount is kept BIG-ENDIAN, as the guest sees it: game code reads it.
// NFS: Carbon's XAudio (sub_82A4E5D8, waiting for a voice the audio worker is
// processing, from a movie thread's voice stop) holds the XAudio lock, reads
// its RecursionCount, leaves the lock that many times and enters it again as
// many. Unleashed kept the field little-endian ("never accessed in guest
// code"): a count of 1 read as 16,777,216, so the thread left 16 million times
// (driving the count deep below zero) and another thread entering in that
// window (EA's sound thread) took the lock and could never give it back: its
// Leave didn't reach 0, so it stayed the owner and the movie thread waited for
// good. The game froze when START skipped a boot movie (the ss-pro1 hang,
// 2026-10-26): rare, as the stop must find the voice mid-pass (3 hangs in 146
// sandboxed boots skipping the PSA and the attract movie, M1 Pro, before and
// after v0.2.0's sleeps alike).
//
// OwningThread stays host-endian (the caller's r13, not the guest KTHREAD
// pointer the 360 keeps there): no guest code is known to read it. LockCount
// is only initialized (-1, the same in either order).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace guestcs
{
    struct Fields
    {
        uint8_t header[16];      // XDISPATCHER_HEADER
        int32_t lockCount;       // -1 (not maintained)
        uint32_t recursionBE;    // big-endian, as guest code reads it
        uint32_t owningThread;   // host-endian: the owner's r13, 0 when free
    };
    static_assert(offsetof(Fields, lockCount) == 16 && offsetof(Fields, recursionBE) == 20 &&
                  offsetof(Fields, owningThread) == 24 && sizeof(Fields) == 28);

    inline uint32_t Swap32(uint32_t v)
    {
        return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
    }

    // Only the owner changes the count, so these need no atomics.
    inline int32_t Recursion(const Fields* cs) { return int32_t(Swap32(cs->recursionBE)); }
    inline void SetRecursion(Fields* cs, int32_t n) { cs->recursionBE = Swap32(uint32_t(n)); }

    inline void Initialize(Fields* cs)
    {
        cs->lockCount = -1;
        SetRecursion(cs, 0);
        cs->owningThread = 0;
    }

    inline bool TryEnter(Fields* cs, uint32_t thisThread)
    {
        std::atomic_ref owner(cs->owningThread);
        uint32_t previous = 0;
        if (owner.compare_exchange_strong(previous, thisThread) || previous == thisThread)
        {
            SetRecursion(cs, Recursion(cs) + 1);
            return true;
        }
        return false;
    }

    inline void Enter(Fields* cs, uint32_t thisThread)
    {
        std::atomic_ref owner(cs->owningThread);
        while (true)
        {
            uint32_t previous = 0;
            if (owner.compare_exchange_weak(previous, thisThread) || previous == thisThread)
            {
                SetRecursion(cs, Recursion(cs) + 1);
                return;
            }
            if (previous != 0)
                owner.wait(previous);
        }
    }

    inline void Leave(Fields* cs)
    {
        int32_t n = Recursion(cs) - 1;
        SetRecursion(cs, n);
        if (n != 0)
            return;
        std::atomic_ref owner(cs->owningThread);
        owner.store(0);
        owner.notify_all();
    }
}
