// Tests for the guest's critical sections (runtime/kernel/critical_section.h,
// header only). From the repo root:
//   clang++ -std=c++20 -O2 -pthread -Iruntime tests/critical_section_test.cpp -o build/critical_section_test
//   build/critical_section_test
// 1. RecursionCount is big-endian in guest memory, as guest code reads it.
// 2. NFS: Carbon's XAudio wait (sub_82A4E5D8): the holder reads the count the
//    guest's way, leaves that many times and enters as many, over and over,
//    while other threads take the lock and give it back. Afterwards the lock
//    is free with a count of 0, and nobody waited for good (the ss-pro1 hang:
//    with the count little-endian, a holder of 1 read 16,777,216 and an
//    entering thread kept the lock for ever). A few seconds.
#include <kernel/critical_section.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

static std::atomic<int> g_failures{ 0 };
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

// The guest's 32-bit load (lwz): big-endian.
static int32_t GuestLoad(const void* p)
{
    const uint8_t* b = static_cast<const uint8_t*>(p);
    return int32_t(uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | uint32_t(b[3]));
}

static int32_t GuestRecursion(const guestcs::Fields& cs)
{
    return GuestLoad(reinterpret_cast<const uint8_t*>(&cs) + 20);
}

static void TestLayout()
{
    printf("guest view of RecursionCount\n");
    guestcs::Fields cs;
    std::memset(&cs, 0xCD, sizeof(cs));
    guestcs::Initialize(&cs);
    CHECK(GuestRecursion(cs) == 0, "%d after Initialize", GuestRecursion(cs));
    CHECK(cs.lockCount == -1, "%d", cs.lockCount);
    CHECK(cs.owningThread == 0, "%08X", cs.owningThread);
    const uint32_t a = 0x6F200260;
    for (int i = 1; i <= 3; i++)
    {
        guestcs::Enter(&cs, a);
        CHECK(GuestRecursion(cs) == i, "%d after %d enters (the guest would read %d)", GuestRecursion(cs), i, GuestRecursion(cs));
    }
    CHECK(cs.owningThread == a, "%08X", cs.owningThread);
    CHECK(!guestcs::TryEnter(&cs, a + 0x200000), "another thread's TryEnter took a held lock");
    CHECK(guestcs::TryEnter(&cs, a) && GuestRecursion(cs) == 4, "the owner's TryEnter: %d", GuestRecursion(cs));
    for (int i = 0; i < 4; i++)
        guestcs::Leave(&cs);
    CHECK(GuestRecursion(cs) == 0 && cs.owningThread == 0, "count %d owner %08X after the last Leave", GuestRecursion(cs), cs.owningThread);
}

static void TestXAudioWait()
{
    printf("XAudio's wait (sub_82A4E5D8): leave as many times as the guest reads, enter again\n");
    guestcs::Fields cs;
    guestcs::Initialize(&cs);
    std::atomic<bool> stop{ false };
    std::atomic<uint64_t> holderRounds{ 0 }, otherEnters{ 0 };
    std::atomic<int> badReads{ 0 };

    // The movie thread: holds the lock (entered twice, as XAudio's API and
    // its voice call do), then the wait loop's release and re-entry.
    std::thread holder([&] {
        const uint32_t self = 0x716002A0;
        while (!stop.load(std::memory_order_relaxed))
        {
            guestcs::Enter(&cs, self);
            guestcs::Enter(&cs, self);
            for (int round = 0; round < 50; round++)
            {
                int32_t n = GuestRecursion(cs);  // lwz r30,24(r28)
                if (n != 2)
                {
                    badReads++;
                    stop = true;  // (16,777,216 leaves: the old bug; don't spin through them)
                    break;
                }
                for (int32_t i = 0; i < n; i++)
                    guestcs::Leave(&cs);
                std::this_thread::yield();
                for (int32_t i = 0; i < n; i++)
                    guestcs::Enter(&cs, self);
                holderRounds++;
            }
            guestcs::Leave(&cs);
            guestcs::Leave(&cs);
        }
    });
    // EA's sound thread and others: in and out of the lock.
    std::vector<std::thread> others;
    for (int t = 0; t < 3; t++)
        others.emplace_back([&, t] {
            const uint32_t self = 0x704002A0 + uint32_t(t) * 0x200000;
            while (!stop.load(std::memory_order_relaxed))
            {
                guestcs::Enter(&cs, self);
                otherEnters++;
                guestcs::Leave(&cs);
            }
        });

    // Watch for a deadlock: progress must continue.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    uint64_t lastRounds = 0, lastEnters = 0;
    bool stuck = false;
    while (std::chrono::steady_clock::now() < deadline && !stop)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        uint64_t r = holderRounds, e = otherEnters;
        if (r == lastRounds && e == lastEnters)
        {
            stuck = true;
            break;
        }
        lastRounds = r;
        lastEnters = e;
    }
    CHECK(!stuck, "no progress for 500 ms: owner %08X, count %d", cs.owningThread, GuestRecursion(cs));
    CHECK(badReads == 0, "the holder read a count of %d, not 2", GuestRecursion(cs));
    stop = true;
    if (stuck)
    {
        printf("  (deadlocked threads left running)\n");
        fflush(stdout);
        std::_Exit(1);
    }
    holder.join();
    for (auto& t : others)
        t.join();
    CHECK(cs.owningThread == 0 && GuestRecursion(cs) == 0, "owner %08X count %d at the end", cs.owningThread, GuestRecursion(cs));
    printf("  %llu wait rounds, %llu enters by the other threads\n", (unsigned long long)holderRounds.load(),
        (unsigned long long)otherEnters.load());
    CHECK(holderRounds > 100 && otherEnters > 100, "too little contention to mean anything");
}

int main()
{
    TestLayout();
    TestXAudioWait();
    if (g_failures)
    {
        printf("%d FAILED\n", g_failures.load());
        return 1;
    }
    printf("all passed\n");
    return 0;
}
