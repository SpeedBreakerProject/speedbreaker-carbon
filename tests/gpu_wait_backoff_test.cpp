// Tests for cpu/gpu_wait_backoff.h, the one GPU-wait back-off of both games:
// when a Direct3D GPU wait's poll sleeps (after the wait has spun 20 us; a gap
// over 200 us starts a new wait), that BackOff() goes through cpu/backoff.h
// (every poll counted, the 50 us sleep timed by backoff::Sleep, nothing when
// NFSMW_GPU_WAIT_BACKOFF=0 turns the kind off) on every platform the header
// compiles for, macOS included since v0.2.0, and that each game's hang-check
// hook backs off before the game's own check: Most Wanted's
// (debug/d3d_waits.cpp) and NFS: Carbon's (game/carbon/d3d_waits.cpp), whose
// lack of it spun a core (both files are read; ctest passes their paths).
// backoff:: is this test's own stand-in (it records the calls), so the test
// needs no runtime. From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/gpu_wait_backoff_test.cpp -o build/gpu_wait_backoff_test
//   build/gpu_wait_backoff_test runtime/debug/d3d_waits.cpp runtime/game/carbon/d3d_waits.cpp
#include <cpu/gpu_wait_backoff.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

// The stand-in for cpu/backoff.cpp.
namespace fake
{
    bool enabled = true;
    int polls = 0, otherKinds = 0;
    std::vector<std::chrono::microseconds> sleeps;
}

namespace backoff
{
    bool Enabled(Kind kind)
    {
        if (kind != kGpuWait)
            fake::otherKinds++;
        return fake::enabled;
    }
    void Sleep(Kind kind, std::chrono::microseconds time)
    {
        if (kind != kGpuWait)
            fake::otherKinds++;
        fake::sleeps.push_back(time);
    }
    void CountGpuWaitPoll()
    {
        fake::polls++;
    }
    void ZeroDelay(Kind) {}
    void CountYield() {}
}

namespace
{
    using namespace std::chrono_literals;
    using gpuwait::Clock;

    void TestShouldSleep()
    {
        gpuwait::BackOffState s;
        const Clock::time_point t0 = Clock::time_point{} + 1h;
        CHECK(!gpuwait::ShouldSleep(s, t0), "a wait's first poll spins");
        CHECK(!gpuwait::ShouldSleep(s, t0 + 10us), "10 us in: spins");
        CHECK(!gpuwait::ShouldSleep(s, t0 + 19us), "19 us in: spins");
        CHECK(gpuwait::ShouldSleep(s, t0 + 20us), "20 us in: sleeps");
        // After a 50 us sleep the next poll is one wait still (BackOff sets lastPoll after its sleep).
        s.lastPoll = t0 + 80us;
        CHECK(gpuwait::ShouldSleep(s, t0 + 90us), "the same wait after a sleep: sleeps");
        CHECK(gpuwait::ShouldSleep(s, t0 + 290us), "a 200 us gap is the same wait");
        CHECK(!gpuwait::ShouldSleep(s, t0 + 491us), "a gap over 200 us is a new wait: spins");
        CHECK(s.waitStart == t0 + 491us, "the new wait starts at its first poll");
        CHECK(!gpuwait::ShouldSleep(s, t0 + 500us), "9 us into the new wait: spins");
        CHECK(gpuwait::ShouldSleep(s, t0 + 512us), "21 us into the new wait: sleeps");
    }

    // Polls back to back for `spin`: how many sleeps BackOff() asked for.
    size_t PollFor(std::chrono::microseconds spin, int& polls)
    {
        fake::sleeps.clear();
        fake::polls = 0;
        auto end = Clock::now() + spin;
        polls = 0;
        while (Clock::now() < end)
        {
            gpuwait::BackOff();
            polls++;
        }
        return fake::sleeps.size();
    }

    void TestBackOff()
    {
        // Let any earlier wait's state lapse: a new wait.
        auto idle = Clock::now() + 1ms;
        while (Clock::now() < idle)
        {
        }
        int polls = 0;
        size_t sleeps = PollFor(2ms, polls);
        CHECK(fake::polls == polls, "every poll counted for the log: %d of %d", fake::polls, polls);
        CHECK(sleeps > 0, "a 2 ms wait sleeps (%d polls)", polls);
        CHECK(sleeps < size_t(polls), "the wait's first 20 us spin: %zu sleeps in %d polls", sleeps, polls);
        bool all50 = true;
        for (auto t : fake::sleeps)
            all50 = all50 && t == 50us;
        CHECK(all50, "each sleep is 50 us");
        CHECK(fake::otherKinds == 0, "only the GPU-wait kind is used");

        // NFSMW_GPU_WAIT_BACKOFF=0 (backoff::Enabled false): spins, still counted.
        fake::enabled = false;
        sleeps = PollFor(2ms, polls);
        CHECK(sleeps == 0, "off: no sleep (%zu)", sleeps);
        CHECK(fake::polls == polls, "off: every poll still counted");
        fake::enabled = true;
    }

    std::string Read(const char* path)
    {
        std::ifstream f(path);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    // The hook (`opening`, then its body) calls gpuwait::BackOff() before the game's own check.
    void TestHook(const char* path, const std::string& opening, const std::string& check)
    {
        std::string text = Read(path);
        CHECK(!text.empty(), "%s unreadable", path);
        CHECK(text.find("#include <cpu/gpu_wait_backoff.h>") != std::string::npos, "%s doesn't include the back-off", path);
        CHECK(text.find("void BackOff(") == std::string::npos, "%s has a back-off of its own", path);
        CHECK(text.find("backoff::Sleep") == std::string::npos, "%s sleeps by itself", path);
        size_t body = text.find(opening + "\n{");
        CHECK(body != std::string::npos, "%s: no %s", path, opening.c_str());
        if (body == std::string::npos)
            return;
        size_t backOff = text.find("gpuwait::BackOff();", body);
        size_t call = text.find(check + "(ctx, base);", body);
        CHECK(backOff != std::string::npos && call != std::string::npos && backOff < call,
            "%s: %s doesn't call gpuwait::BackOff() before %s", path, opening.c_str(), check.c_str());
    }
}

int main(int argc, char** argv)
{
    printf("gpu_wait_backoff_test\n");
    TestShouldSleep();
    TestBackOff();
    if (argc >= 3)
    {
        TestHook(argv[1], "SB_HOOK(sub_825A5B20)", "SB_ORIGINAL(sub_825A5B20)");  // Most Wanted
        TestHook(argv[2], "PPC_FUNC(sub_826E0088)", "__imp__sub_826E0088");       // NFS: Carbon
    }
    else
        printf("  (the hooks' files not given: their checks skipped)\n");
    if (g_failures)
    {
        printf("%d FAILED\n", g_failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
