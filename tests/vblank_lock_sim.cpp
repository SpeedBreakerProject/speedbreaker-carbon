// Closed-loop simulation of the guest vblank lock with the runtime's own
// RefreshFit and VblankLock (video/refresh_fit.cpp, video/vblank_lock.cpp).
// From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/vblank_lock_sim.cpp runtime/video/refresh_fit.cpp \
//     runtime/video/vblank_lock.cpp -o build/vblank_lock_sim
//   build/vblank_lock_sim [--multiples]
// (--multiples: only the displays at a whole multiple of 60 Hz, and the
// planted faults; that part is the ctest. The 60 Hz tables print as in v0.1.6.)
//
// Model (times in ns):
//   display   refreshes every P (true rate), phase random; a present made
//             before R_k - latch shows at R_k (FIFO, in order); its
//             completion is seen at R_k + notify (0.2-0.6 ms, sometimes late).
//   guest     each vblank (VblankLock::Next, or free-running 16.667 ms)
//             releases one game frame, which reaches the presenter x later
//             (x drawn per scene). The presenter keeps only the newest
//             pending frame (older ones are dropped), picks it up ~0.5 ms
//             later, and presents at once when a swapchain image is free
//             (3 images: one on screen, at most 2 queued), else waits for
//             the next flip holding it.
//   variable  (the second table) no grid: a present shows as soon as it is
//             latched, no sooner than 1/144 s after the last refresh, and
//             the display refreshes on its own after 1/48 s without one.
// Counted from the 60th second (the lock has converged by then): dropped
// frames, repeats (a refresh with no new frame while frames flow), slips,
// the median vblank -> on screen latency, and the guest vblank's rate.
//   suspended (the last tables) the game is suspended (iOS: Control Center,
//             the app switcher): no vblank fires and its frames stand still,
//             the display shows what was queued, and the next vblank comes
//             exactly the time suspended later, then VblankLock::Resumed
//             (gpu/command_processor.cpp's Vsync thread).
//   multiple  (--multiples) a display at 2-4 times 60 Hz (120, 180, 240) with
//             its multiple told to RefreshFit (the display mode's rate: video/
//             refresh_multiple.h). A present still shows on the first refresh
//             after it (the display has spare refreshes, so nothing queues);
//             the presenter holds a new frame until VblankLock::PresentAt.
//             Counted per new frame: refreshes on screen against the vblanks'
//             (uneven: not `multiple` a vblank), and frames that couldn't make
//             their refresh (ready after the latch of the refresh `multiple`
//             after the one before their vblank, the 60 Hz frame's budget):
//             each such miss may cost two uneven frames (one long, the next
//             short), anything more is the cadence's own fault. Optionally the
//             vblank thread wakes late (Windows: 1.26 +- 0.45 ms), Frame Rate
//             30 (two lock periods a vblank), and planted faults.
#include <video/refresh_fit.h>
#include <video/vblank_lock.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <iterator>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace video;

struct Scene
{
    const char* name;
    double meanMs, sdMs;   // vblank -> presenter, normal
    double spikeP, spikeMs; // occasional slow frames
};

// What --multiples changes about a run (all off: the v0.1.6 model, unchanged).
struct Options
{
    int multiple = 0;         // told to RefreshFit (0: not told, as v0.1.6)
    int trueMultiple = 0;     // the display's own, for the cadence count (0: none: 144 Hz, 90 Hz)
    bool hold = true;         // the presenter holds new frames (VblankLock::PresentAt)
    int64_t holdShiftNs = 0;  // planted: the hold moved this much
    double wakeMeanMs = 0, wakeSdMs = 0;  // the vblank thread wakes this late (clamped 0.3-4 ms)
    int divider = 1;          // 2: Frame Rate 30
    double vrrMaxHz = 144;    // the variable-refresh panel's top
};

struct Result
{
    double lockAtS = -1;
    uint64_t drops = 0, repeats = 0, frames = 0, slips = 0;
    double latMedMs = 0;
    double hz = 0;
    double followsAtS = -1;  // the lock found the display following the vblank
    double vblankHz = 0;     // counted window
    // After the last suspension: vblanks until the phase was reached again,
    // drops plus repeats from the first frame shown to 5 s after the resume,
    // and whether the lock held.
    int64_t rephaseVblanks = -1;
    uint64_t afterResume = 0;
    bool lockedAtEnd = false;
    // --multiples: new frames counted, uneven ones, frames that couldn't make
    // their refresh, refreshes on screen -> frames, lock changes.
    uint64_t newFrames = 0, uneven = 0, missed = 0, lockChanges = 0;
    uint64_t shownFor[8] = {};
};

static Result Run(double displayHz, bool lockOn, const Scene& scene, double seconds, uint32_t seed, double countFromS,
    bool variable = false, const std::vector<std::pair<double, double>>& suspensions = {}, bool callResumed = true,
    const Options& opt = Options{})
{
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> nd(0, 1);
    std::uniform_real_distribution<double> ud(0, 1);
    const double P = 1e9 / displayHz;
    const int64_t t0 = 1'000'000'000'000;
    const double phase = ud(rng) * P;
    const double latch = 2.0e6;  // compositor latches this long before the refresh

    RefreshFit fit;
    VblankLock lock(16'667'000, lockOn);
    if (opt.multiple)
        fit.SetMultiple(opt.multiple);
    lock.SetHold(opt.hold);
    const int truth = std::max(opt.trueMultiple, 1);

    struct Frame
    {
        int64_t vblank, arrive;
    };
    std::deque<Frame> inflight;      // released, not yet at the presenter (sorted by arrive)
    bool pendingFull = false;
    Frame pending{};
    bool held = false;               // main thread holds a frame, waiting for an image
    Frame heldFrame{};
    int64_t wake = INT64_MAX;        // it holds the frame until then (VblankLock::PresentAt)
    struct Queued
    {
        Frame f;
        int64_t presented;
    };
    std::deque<Queued> queue;        // presented, not yet on screen
    Result res;
    std::vector<double> lat;

    int64_t vblank = t0;  // scheduled (VblankLock::Next)
    int64_t fired = t0;   // when it fires (Options' late wake-ups)
    auto wakeLate = [&]() -> int64_t {
        if (opt.wakeMeanMs <= 0)
            return 0;
        return int64_t(std::clamp(opt.wakeMeanMs + opt.wakeSdMs * nd(rng), 0.3, 4.0) * 1e6);
    };
    int64_t k = 1;
    int64_t lastShownK = -1, lastShownVblank = 0;
    bool wasLocked = false;
    int64_t refresh = t0 + int64_t(phase);
    int64_t lastRefresh = refresh;
    const int64_t end = t0 + int64_t(seconds * 1e9);
    int64_t firstCounted = 0, lastCounted = 0;
    uint64_t vblanks = 0;
    const int64_t countFrom = t0 + int64_t(countFromS * 1e9);
    bool flowing = false;
    // Suspensions as (start, end), in order; `pause` is the one under way or next.
    std::vector<std::pair<int64_t, int64_t>> pauses;
    for (auto [at, length] : suspensions)
        pauses.push_back({ t0 + int64_t(at * 1e9), t0 + int64_t((at + length) * 1e9) });
    size_t pause = 0;
    bool suspended = false;
    int64_t resumedAt = -1, sinceResume = 0, shownAfterResume = -1;  // the last resume
    auto afterResume = [&](int64_t t) { return shownAfterResume >= 0 && t <= resumedAt + 5'000'000'000; };
    auto present = [&](const Frame& f, int64_t at) {
        queue.push_back({ f, at });
    };
    auto presenterStep = [&](int64_t now) {
        // Main thread: present what it holds when an image is free (and,
        // at a multiple, its hold is over), then take the pending frame.
        while (true)
        {
            if (held)
            {
                if (queue.size() >= 2)
                    return;
                if (opt.multiple)
                {
                    const int64_t at = lock.PresentAt(heldFrame.vblank);
                    if (at && at + opt.holdShiftNs > now)
                    {
                        wake = at + opt.holdShiftNs;
                        return;
                    }
                    wake = INT64_MAX;
                }
                present(heldFrame, now);
                held = false;
            }
            if (!pendingFull)
                return;
            heldFrame = pending;
            pendingFull = false;
            held = true;
        }
    };
    while (refresh < end)
    {
        if (variable && k > 1)
        {
            const int64_t minGap = int64_t(1e9 / opt.vrrMaxHz), maxGap = int64_t(1e9 / 48);
            refresh = lastRefresh + maxGap;
            if (!queue.empty())
                refresh = std::clamp(queue.front().presented + int64_t(latch), lastRefresh + minGap, lastRefresh + maxGap);
        }
        // Next event: vblank, a frame's arrival, or a flip.
        int64_t nextArrive = inflight.empty() ? INT64_MAX : inflight.front().arrive;
        const int64_t nextEvent = std::min({ fired, nextArrive, refresh, wake });
        if (pause < pauses.size() && !suspended && nextEvent >= pauses[pause].first)
        {
            // Everything the game does stands still for the suspension; the
            // display flips on through what was already queued.
            suspended = true;
            vblank += pauses[pause].second - pauses[pause].first;
            fired += pauses[pause].second - pauses[pause].first;
            if (wake != INT64_MAX)
                wake += pauses[pause].second - pauses[pause].first;
            for (Frame& f : inflight)
                f.arrive += pauses[pause].second - pauses[pause].first;
            continue;
        }
        if (suspended && nextEvent >= pauses[pause].second)
        {
            suspended = false;
            resumedAt = pauses[pause++].second;
            sinceResume = 0;
            shownAfterResume = -1;
            res.rephaseVblanks = -1;
            res.afterResume = 0;
            if (callResumed)
                lock.Resumed();
            presenterStep(resumedAt);
            continue;
        }
        if (wake <= refresh && wake <= nextArrive && wake < fired)
        {
            const int64_t at = wake;
            wake = INT64_MAX;
            presenterStep(at);
            continue;
        }
        if (fired <= refresh && fired <= nextArrive)
        {
            double x = scene.meanMs + scene.sdMs * nd(rng);
            if (ud(rng) < scene.spikeP)
                x = scene.spikeMs + 1.0 * nd(rng);
            x = std::max(1.0, x);
            Frame f{ fired, fired + int64_t(x * 1e6 + 500'000) };  // +0.5 ms pickup
            auto it = inflight.end();
            while (it != inflight.begin() && std::prev(it)->arrive > f.arrive)
                --it;
            inflight.insert(it, f);
            if (vblank >= countFrom)
            {
                firstCounted = vblanks++ ? firstCounted : vblank;
                lastCounted = vblank;
            }
            vblank = lock.Next(vblank);
            for (int d = 1; d < opt.divider; d++)  // Frame Rate 30: every other lock period
                vblank = lock.Next(vblank);
            fired = vblank + wakeLate();
            if (resumedAt >= 0 && res.rephaseVblanks < 0)
            {
                sinceResume++;
                if (std::abs(lock.GetStatus().errorNs) <= double(VblankLock::kMaxStepNs))
                    res.rephaseVblanks = sinceResume;
            }
            continue;
        }
        if (nextArrive <= refresh)
        {
            Frame f = inflight.front();
            inflight.pop_front();
            if (pendingFull && f.vblank > pending.vblank && pending.vblank >= countFrom)
                res.drops++;
            if (pendingFull && f.vblank > pending.vblank && afterResume(f.arrive))
                res.afterResume++;
            if (!pendingFull || f.vblank > pending.vblank)
                pending = f;
            pendingFull = true;
            presenterStep(f.arrive);
            continue;
        }
        // Flip at `refresh`: the oldest present made before the latch shows.
        if (!queue.empty() && queue.front().presented <= refresh - int64_t(latch))
        {
            Queued q = queue.front();
            queue.pop_front();
            double notify = 200'000 + 400'000 * ud(rng);
            if (ud(rng) < 0.01)
                notify += 2e6 * ud(rng);
            int64_t onScreen = refresh + int64_t(notify);
            fit.Add(onScreen);
            lock.SetDisplay(fit.Estimate());
            // The presenter doesn't time a frame released before the resume
            // (video/presenter.cpp's OnScreen).
            if (q.f.vblank >= resumedAt)
                lock.Observe(q.f.vblank, q.presented, onScreen, std::min(q.f.arrive, q.presented));
            if (resumedAt >= 0 && shownAfterResume < 0)
                shownAfterResume = refresh;
            if (q.f.vblank >= countFrom)
            {
                res.frames++;
                lat.push_back((refresh - q.f.vblank) / 1e6);
            }
            if (opt.multiple || opt.trueMultiple)
            {
                // Its predecessor's refreshes on screen against the vblanks
                // between them, and whether it could have made its refresh at
                // all (the display's true phase; `truth` refreshes a vblank).
                if (lastShownK >= 0 && q.f.vblank >= countFrom && !afterResume(refresh))
                {
                    const int64_t shown = k - lastShownK;
                    const int64_t wanted = std::max<int64_t>(1, std::llround(double(q.f.vblank - lastShownVblank) / (P * truth))) * truth;
                    res.newFrames++;
                    res.uneven += shown != wanted;
                    res.shownFor[std::min<int64_t>(shown, 7)]++;
                    const double before = std::floor((double(q.f.vblank) - (double(t0) + phase)) / P);
                    const double target = double(t0) + phase + (before + truth) * P;
                    res.missed += double(q.f.arrive) > target - latch;
                }
                lastShownK = k;
                lastShownVblank = q.f.vblank;
            }
            flowing = true;
            if (!suspended)
                presenterStep(refresh);
        }
        else
        {
            if (flowing && refresh >= countFrom)
                res.repeats++;
            if (afterResume(refresh))
                res.afterResume++;
        }
        if (res.lockAtS < 0 && lock.Locked())
            res.lockAtS = (refresh - t0) / 1e9;
        if (lock.Locked() != wasLocked)
        {
            wasLocked = !wasLocked;
            res.lockChanges++;
        }
        if (res.followsAtS < 0 && lock.GetStatus().follows)
            res.followsAtS = (refresh - t0) / 1e9;
        k++;
        lastRefresh = refresh;
        if (!variable)
            refresh = t0 + int64_t(phase + double(k) * P);
    }
    res.vblankHz = vblanks > 1 ? double(vblanks - 1) / (double(lastCounted - firstCounted) / 1e9) : 0.0;
    auto st = lock.GetStatus();
    res.slips = st.slips;
    res.lockedAtEnd = st.locked;
    res.hz = st.display.periodNs > 0 ? 1e9 / st.display.periodNs : 0;
    if (!lat.empty())
    {
        std::nth_element(lat.begin(), lat.begin() + lat.size() / 2, lat.end());
        res.latMedMs = lat[lat.size() / 2];
    }
    return res;
}

static bool Multiples();

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "--multiples")
        return Multiples() ? 0 : 1;
    const Scene scenes[] = {
        { "light (vblank->presenter 7+-1.5 ms)", 7, 1.5, 0.0, 0 },
        { "driving (9+-2 ms, 1% at 20 ms)", 9, 2, 0.01, 20 },
        { "fly-in (14+-1.5 ms, 2% at 20 ms)", 14, 1.5, 0.02, 20 },
        { "heavy (16+-1.5 ms: often a refresh late)", 16, 1.5, 0.0, 0 },
    };
    const double rates[] = { 59.972616, 59.0, 61.0 };
    const double seconds = 300, countFrom = 60;
    printf("%.0f s per run, counted from %.0f s (after the lock converges); 3 seeds each\n", seconds, countFrom);
    printf("%-44s %-10s %5s %8s %8s %8s %6s %8s\n", "scene", "display", "lock", "lock at", "drops", "repeats", "slips", "lat ms");
    bool ok = true;
    for (const Scene& s : scenes)
        for (double hz : rates)
            for (bool on : { false, true })
            {
                Result sum;
                double lockAt = 0, latMed = 0;
                for (uint32_t seed = 1; seed <= 3; seed++)
                {
                    Result r = Run(hz, on, s, seconds, seed * 7919 + uint32_t(hz * 10), countFrom);
                    sum.drops += r.drops;
                    sum.repeats += r.repeats;
                    sum.slips += r.slips;
                    lockAt += r.lockAtS;
                    latMed += r.latMedMs;
                    ok &= !on || (r.lockAtS > 0 && r.followsAtS < 0);  // a real grid is never let go
                }
                printf("%-44s %-10.4f %5s %8.1f %8llu %8llu %6llu %8.1f\n", s.name, hz, on ? "on" : "off", on ? lockAt / 3 : 0.0,
                    (unsigned long long)sum.drops, (unsigned long long)sum.repeats, (unsigned long long)sum.slips, latMed / 3);
            }

    // A display that follows the presents: steady frames fit the guest's own
    // rate well enough to lock, and the lock must let go rather than chase
    // it. Jittery ones never fit. The guest vblank should end at 60 Hz.
    const Scene steady[] = {
        { "steady (vblank->presenter 3+-0.05 ms)", 3, 0.05, 0.0, 0 },
        { "light (vblank->presenter 7+-1.5 ms)", 7, 1.5, 0.0, 0 },
    };
    printf("\nvariable refresh (48-144 Hz), lock on; %.0f s per run, counted from %.0f s\n", seconds, countFrom);
    printf("%-44s %5s %8s %10s %10s %8s %8s\n", "scene", "seed", "lock at", "let go at", "vblank Hz", "drops", "repeats");
    for (const Scene& s : steady)
        for (uint32_t seed = 1; seed <= 3; seed++)
        {
            Result r = Run(60.0, true, s, seconds, seed * 104729, countFrom, true);
            printf("%-44s %5u %8.1f %10.1f %10.4f %8llu %8llu\n", s.name, seed, r.lockAtS, r.followsAtS, r.vblankHz,
                (unsigned long long)r.drops, (unsigned long long)r.repeats);
            ok &= std::abs(r.vblankHz - 1e9 / 16'667'000) < 0.01;
        }
    printf("%s\n", ok ? "fixed rates stay locked; variable refresh ends free-running at 60 Hz: ok" : "FAILED");

    // The vblank comes back up to half a refresh off the grid: the lock walks
    // it back a step a vblank, within ~170 vblanks, without deciding that the
    // display follows the vblank, and shows at most one frame twice or not at
    // all on the way.
    const double suspensions[] = { 0.1, 0.5, 6.0013, 12.5, 30.0 };
    printf("\nsuspended at 90 s, lock on; 3 seeds each, max of the three\n");
    printf("%-44s %-10s %9s %8s %14s %6s\n", "scene", "display", "suspended", "rephase", "drops+repeats", "lock");
    bool resumes = true;
    for (const Scene& s : scenes)
        for (double hz : rates)
            for (double d : suspensions)
            {
                int64_t rephase = 0;
                uint64_t after = 0;
                bool held = true;
                // Half a refresh of steps, and a few more: each error is
                // measured before its step, and the fitted grid moves a little.
                const int64_t limit = int64_t(std::ceil(1e9 / hz / 2 / double(VblankLock::kMaxStepNs))) + 10;
                for (uint32_t seed = 1; seed <= 3; seed++)
                {
                    Result r = Run(hz, true, s, 90 + d + 10, seed * 6151 + uint32_t(hz * 10), countFrom, false, { { 90, d } });
                    rephase = r.rephaseVblanks < 0 ? INT64_MAX : std::max(rephase, r.rephaseVblanks);
                    after = std::max(after, r.afterResume);
                    held &= r.lockedAtEnd && r.followsAtS < 0;
                }
                printf("%-44s %-10.4f %9.4f %8lld %14llu %6s\n", s.name, hz, d, rephase == INT64_MAX ? -1LL : (long long)rephase,
                    (unsigned long long)after, held ? "held" : "LOST");
                resumes &= rephase <= limit && after <= 1 && held;
            }

    // Suspended again while the lock is still walking back (Control Center
    // pulled down a few times in a row). Resumed() starts each walk's chase
    // count afresh; without it the walks add up to "the display follows the
    // vblank", and the lock is gone for the rest of the run.
    printf("\nsuspended 5 times, each 2-2.7 s after the last resume, light scene; 3 seeds each\n");
    printf("%-10s %9s %5s %10s %16s\n", "display", "suspended", "gap", "lock held", "without Resumed");
    for (double hz : rates)
        for (double d : { 0.5, 12.5 })
            for (double gap : { 2.0, 2.7 })
            {
                std::vector<std::pair<double, double>> pauses;
                for (int i = 0; i < 5; i++)
                    pauses.push_back({ 90 + i * (d + gap), d });
                int held = 0, heldWithout = 0;
                for (uint32_t seed = 1; seed <= 3; seed++)
                    for (bool call : { true, false })
                    {
                        Result r = Run(hz, true, scenes[0], 90 + 5 * (d + gap) + 10, seed * 6151 + uint32_t(hz * 10), countFrom, false,
                            pauses, call);
                        (call ? held : heldWithout) += r.lockedAtEnd && r.followsAtS < 0;
                    }
                printf("%-10.4f %9.4f %5.1f %5d of 3 %11d of 3\n", hz, d, gap, held, heldWithout);
                resumes &= held == 3;
            }
    printf("%s\n", resumes ? "after a suspension the lock re-phases within ~170 vblanks, holds, and drops or repeats at most once: ok"
                            : "FAILED");
    return ok && resumes ? 0 : 1;
}

// ---- Displays at a whole multiple of 60 Hz (--multiples) ----
namespace
{
    const Scene kScenes[] = {
        { "fast (1.5+-0.3 ms: Steam Machine)", 1.5, 0.3, 0.0, 0 },
        { "light (7+-1.5 ms)", 7, 1.5, 0.0, 0 },
        { "driving (9+-2 ms, 1% at 20)", 9, 2, 0.01, 20 },
        { "fly-in (14+-1.5 ms, 2% at 20)", 14, 1.5, 0.02, 20 },
        { "heavy (16+-1.5 ms)", 16, 1.5, 0.0, 0 },
    };
    constexpr double kSeconds = 180, kCountFrom = 60;

    struct Row
    {
        Result sum;
        double lockAt = 0, lat = 0, vblankHz = 0;
        bool locked = true, follows = false;
        int seeds = 0;
    };

    Row RunSeeds(double hz, const Scene& s, const Options& o, int seeds, uint32_t salt, double seconds = kSeconds,
        bool variable = false)
    {
        Row row;
        for (int seed = 1; seed <= seeds; seed++)
        {
            Result r = Run(hz, true, s, seconds, uint32_t(seed) * 7919 + uint32_t(hz * 10) + salt, kCountFrom, variable, {}, true, o);
            row.sum.newFrames += r.newFrames;
            row.sum.uneven += r.uneven;
            row.sum.missed += r.missed;
            row.sum.slips += r.slips;
            row.sum.lockChanges += r.lockChanges;
            for (int i = 0; i < 8; i++)
                row.sum.shownFor[i] += r.shownFor[i];
            row.lockAt = std::max(row.lockAt, r.lockAtS);
            row.lat += r.latMedMs / seeds;
            row.vblankHz += r.vblankHz / seeds;
            row.locked &= r.lockAtS > 0 && r.lockedAtEnd;
            row.follows |= r.followsAtS >= 0;
            row.seeds++;
        }
        return row;
    }

    double Pct(uint64_t a, uint64_t b) { return b ? 100.0 * double(a) / double(b) : 0.0; }

    std::string Histogram(const Result& r)
    {
        uint64_t all = 0;
        for (uint64_t c : r.shownFor)
            all += c;
        std::string out;
        for (int i = 1; i < 8; i++)
            if (r.shownFor[i] * 1000 >= all && r.shownFor[i])
            {
                char b[32];
                snprintf(b, sizeof b, "%s%d:%.1f%%", out.empty() ? "" : " ", i, Pct(r.shownFor[i], all));
                out += b;
            }
        return out;
    }

    // The fix's criteria for a fixed-refresh multiple: locked (and never let
    // go), the vblank at the display's rate over the multiple, at most 0.3%
    // of frames uneven, and, for a scene without spikes, the least latency
    // its frames allow: the first refresh after the vblank's that a frame
    // ready at the scene's 3 sigma (+0.5 ms pickup, + the wake-up's) makes
    // with the 2 ms latch (vblank -> screen = that many refreshes less 1 ms).
    bool Judge(const Row& r, double hz, int m, const Scene& s, const Options& o, std::string* why = nullptr)
    {
        const double vblankWant = hz / m;
        const bool rate = std::abs(r.vblankHz - vblankWant) < 0.01;
        const bool even = r.sum.uneven * 1000 <= r.sum.newFrames * 3;  // 0.3%
        const double refreshMs = 1e3 / hz;
        const double need = s.meanMs + 3 * s.sdMs + 0.5 + o.wakeMeanMs + 3 * o.wakeSdMs + 1.0 + 2.0;
        const double least = std::ceil(need / refreshMs) * refreshMs - 1.0;
        const bool latency = s.spikeP > 0 || r.lat <= least + 0.5;
        if (why)
            *why = std::string(!r.locked ? "not locked; " : "") + (r.follows ? "let go; " : "") + (!rate ? "vblank rate; " : "") +
                (!even ? "uneven; " : "") + (!latency ? "latency; " : "");
        return r.locked && !r.follows && rate && even && latency;
    }

    void PrintRow(const char* display, const Scene& s, const Row& r, const char* verdict)
    {
        printf("%-12s %-30s %7.1f %6llu %7.2f %7.2f %6llu %6.1f %10.4f  %-24s %s\n", display, s.name, r.lockAt,
            (unsigned long long)r.sum.lockChanges, Pct(r.sum.uneven, r.sum.newFrames), Pct(r.sum.missed, r.sum.newFrames),
            (unsigned long long)r.sum.slips, r.lat, r.vblankHz, Histogram(r.sum).c_str(), verdict);
    }

    void Header()
    {
        printf("%-12s %-30s %7s %6s %7s %7s %6s %6s %10s  %-24s %s\n", "display", "scene", "lock at", "lockch", "uneven%", "missed%",
            "slips", "lat ms", "vblank Hz", "refreshes:frames", "");
    }
}

static bool Multiples()
{
    bool ok = true;
    printf("displays at a whole multiple of 60 Hz: %.0f s per run, counted from %.0f s; 3 seeds each, summed (lock at: the latest)\n"
           "  uneven%%: new frames not on screen for `multiple` refreshes a vblank; missed%%: frames ready after their refresh's latch\n"
           "  (they're held for later refreshes then: VblankLock::AdaptHold); lat: vblank -> on screen, median\n", kSeconds, kCountFrom);
    Header();
    const double rates[] = { 119.88, 120.0, 180.0, 239.76, 240.0 };
    for (double hz : rates)
    {
        const int m = RefreshFit::MultipleOf(hz);
        for (const Scene& s : kScenes)
        {
            Options o;
            o.multiple = o.trueMultiple = m;
            Row r = RunSeeds(hz, s, o, 3, 0);
            std::string why;
            const bool pass = Judge(r, hz, m, s, o, &why);
            char display[16];
            snprintf(display, sizeof display, "%.2f/%d", hz, m);
            PrintRow(display, s, r, pass ? "ok" : ("FAIL: " + why).c_str());
            ok &= pass;
        }
    }

    printf("\nthe vblank thread waking late as on Windows (1.26 +- 0.45 ms, sleep_until after timeBeginPeriod(1)); 3 seeds\n");
    Header();
    for (double hz : { 120.0, 240.0 })
        for (int i : { 0, 1, 2 })
        {
            const int m = RefreshFit::MultipleOf(hz);
            Options o;
            o.multiple = o.trueMultiple = m;
            o.wakeMeanMs = 1.26;
            o.wakeSdMs = 0.45;
            Row r = RunSeeds(hz, kScenes[i], o, 3, 17);
            std::string why;
            const bool pass = Judge(r, hz, m, kScenes[i], o, &why);
            char display[16];
            snprintf(display, sizeof display, "%.0f/%d win", hz, m);
            PrintRow(display, kScenes[i], r, pass ? "ok" : ("FAIL: " + why).c_str());
            ok &= pass;
        }

    printf("\nFrame Rate 30 (a guest vblank every 2 lock periods) at 120 and 240 Hz: 4 and 8 refreshes a frame; 3 seeds\n");
    Header();
    for (double hz : { 120.0, 240.0 })
    {
        const int m = RefreshFit::MultipleOf(hz);
        Options o;
        o.multiple = o.trueMultiple = m;
        o.divider = 2;
        Row r = RunSeeds(hz, kScenes[2], o, 3, 29);
        // The vblank comes every 2 guest periods; frames stay 2m refreshes.
        const bool pass = r.locked && !r.follows && std::abs(r.vblankHz - hz / m / 2) < 0.01 &&
            r.sum.uneven * 1000 <= r.sum.newFrames * 3 && r.sum.shownFor[std::min(2 * m, 7)] * 100 >= r.sum.newFrames * 99;
        char display[16];
        snprintf(display, sizeof display, "%.0f/%d fr30", hz, m);
        PrintRow(display, kScenes[2], r, pass ? "ok" : "FAIL");
        ok &= pass;
    }

    printf("\nno whole multiple (the mode's multiple is 0, so the fit is told nothing, as v0.1.6): never locked, frames 2-3 (or 1-2)\n"
           "refreshes each as the rate dictates; 3 seeds\n");
    Header();
    for (double hz : { 143.9, 144.0, 165.0, 90.0 })
        for (int i : { 1, 2 })
        {
            Options o;
            o.multiple = std::max(RefreshFit::MultipleOf(hz), 1);  // 0 -> 1, what the presenter tells the fit
            Row r = RunSeeds(hz, kScenes[i], o, 3, 41);
            const bool pass = r.lockAt <= 0 && std::abs(r.vblankHz - 1e9 / 16'667'000) < 0.01;
            char display[16];
            snprintf(display, sizeof display, "%.1f", hz);
            PrintRow(display, kScenes[i], r, pass ? "ok (free-running)" : "FAIL: locked");
            ok &= pass;
        }

    printf("\nvariable refresh up to 120 Hz in a 120 Hz mode (told multiple 2): the display follows the presents; the lock lets go\n"
           "(or never takes it) in every run and the vblank ends free-running at 60 Hz (counted 120-300 s); 3 seeds\n");
    {
        const Scene steady[] = { { "steady (3+-0.05 ms)", 3, 0.05, 0.0, 0 }, kScenes[0], kScenes[1] };
        Header();
        for (const Scene& s : steady)
        {
            Options o;
            o.multiple = 2;
            o.vrrMaxHz = 120;
            Row row;
            bool every = true;
            for (int seed = 1; seed <= 3; seed++)
            {
                Result r = Run(60.0, true, s, 300, uint32_t(seed) * 7919 + 600 + 53, 120, true, {}, true, o);
                every &= r.followsAtS >= 0 || r.lockAtS <= 0;
                row.sum.newFrames += r.newFrames;
                row.sum.uneven += r.uneven;
                row.sum.lockChanges += r.lockChanges;
                for (int i = 0; i < 8; i++)
                    row.sum.shownFor[i] += r.shownFor[i];
                row.lockAt = std::max(row.lockAt, std::max(r.followsAtS, r.lockAtS));
                row.lat += r.latMedMs / 3;
                row.vblankHz += r.vblankHz / 3;
                row.follows |= r.followsAtS >= 0;
            }
            const bool pass = every && std::abs(row.vblankHz - 1e9 / 16'667'000) < 0.001;
            PrintRow("VRR 48-120", s, row, pass ? (row.follows ? "ok (let go; lock at: the latest let-go)" : "ok (never locked)")
                                                : "FAIL: not free-running at 60 Hz");
            ok &= pass;
        }
    }

    printf("\nsuspended at 90 s for 0.5 / 6.0013 / 12.5 s at 120 and 240 Hz (fast, light): re-phased within half a refresh of\n"
           "steps and still locked; 3 seeds each, max\n");
    for (int sc : { 0, 1 })
        for (double hz : { 120.0, 240.0 })
            for (double d : { 0.5, 6.0013, 12.5 })
            {
                const int m = RefreshFit::MultipleOf(hz);
                const int64_t limit = int64_t(std::ceil(1e9 / hz / 2 / double(VblankLock::kMaxStepNs))) + 10;
                int64_t rephase = 0;
                bool held = true;
                for (uint32_t seed = 1; seed <= 3; seed++)
                {
                    Options o;
                    o.multiple = o.trueMultiple = m;
                    Result r = Run(hz, true, kScenes[sc], 90 + d + 10, seed * 6151 + uint32_t(hz * 10), kCountFrom, false, { { 90, d } }, true, o);
                    rephase = r.rephaseVblanks < 0 ? INT64_MAX : std::max(rephase, r.rephaseVblanks);
                    held &= r.lockedAtEnd && r.followsAtS < 0;
                }
                const bool pass = rephase <= limit && held;
                printf("  %-30s %6.1f Hz suspended %7.4f s: rephase %lld vblanks (limit %lld), lock %s: %s\n", kScenes[sc].name, hz, d,
                    rephase == INT64_MAX ? -1LL : (long long)rephase, (long long)limit, held ? "held" : "LOST", pass ? "ok" : "FAIL");
                ok &= pass;
            }

    // Planted faults: each must fail the criteria above in at least one of
    // these rows (the fast, light and driving scenes at 119.88 and 240 Hz).
    printf("\nplanted faults (each must fail a row):\n");
    struct Plant
    {
        const char* name;
        void (*apply)(Options&, double hz);
    };
    const Plant plants[] = {
        { "v0.1.6: the mode's multiple unused (the fit at 1)", [](Options& o, double) { o.multiple = 1; } },
        { "the lock without the hold (frames when ready)", [](Options& o, double) { o.hold = false; } },
        { "the hold a refresh early", [](Options& o, double hz) { o.holdShiftNs = -int64_t(1e9 / hz); } },
        { "the hold a refresh late", [](Options& o, double hz) { o.holdShiftNs = int64_t(1e9 / hz); } },
        { "the fit told half the multiple", [](Options& o, double) { o.multiple = std::max(o.multiple / 2, 1); } },
    };
    bool plantsOk = true;
    for (const Plant& p : plants)
    {
        int failed = 0, rows = 0;
        for (double hz : { 119.88, 240.0 })
            for (int i : { 0, 1, 2 })
            {
                const int m = RefreshFit::MultipleOf(hz);
                Options o;
                o.multiple = o.trueMultiple = m;
                p.apply(o, hz);
                Row r = RunSeeds(hz, kScenes[i], o, 1, 67);
                std::string why;
                failed += !Judge(r, hz, m, kScenes[i], o, &why);
                rows++;
                printf("    %-52s %7.2f %-30s uneven %6.2f%% missed %5.2f%% lat %5.1f: %s\n", p.name, hz, kScenes[i].name,
                    Pct(r.sum.uneven, r.sum.newFrames), Pct(r.sum.missed, r.sum.newFrames), r.lat, why.empty() ? "passes" : why.c_str());
            }
        printf("  plant: %-52s %s (%d of %d rows fail)\n", p.name, failed ? "caught" : "NOT caught", failed, rows);
        plantsOk &= failed > 0;
    }
    printf("%s\n", ok && plantsOk ? "multiples: locked to every 2nd-4th refresh, even, the least latency the frames allow; "
                                    "non-multiples and variable refresh free-run; every plant caught: ok"
                                  : "FAILED");
    return ok && plantsOk;
}
