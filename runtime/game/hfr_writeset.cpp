// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// DIAGNOSTIC for HFR (game/hfr.cpp), off unless NFSMW_HFR_WRITESET is set:
// what an in-between picture leaves changed in guest memory once its state
// has been put back, against what a real picture's render changes and what
// the other threads change meanwhile. A leak (guest state the in-between
// picture changes that the world reads after it) would make HFR play
// differently from 60 fps.
//
// NFSMW_HFR_WRITESET=<frame>,<count>[,<file>]: from that frame of the render
// entry, `count` measurements, each three frames long:
//   1. a frame with an in-between picture: all committed guest memory (the
//      three heaps and the XEX image, 0x82000000-0x82CD0000) copied just
//      before the in-between picture's state is saved; compared once its
//      state has been put back (B: what it left changed), and again after the
//      frame's real picture has been played (R: what the real one changed
//      besides, and which B bytes it wrote again);
//   2. a frame without one (the control): copied before the real picture,
//      compared after it has been played (C: a real render's writes, with
//      the other threads');
//   3. a frame where nothing is drawn between the copy and the comparison
//      but a 10 ms sleep (N: the other threads alone).
// Each changed run of bytes (runs closer than 64 bytes merged) goes to the
// file (default hfr_writeset.txt in the working directory) with its
// allocation; a summary line per measurement goes to stderr: B's bytes in
// runs that neither C nor N touch are the leak candidates.
#include <stdafx.h>
#include <kernel/function.h>
#include <kernel/vmem.h>

#include "hfr.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace
{
    struct Config
    {
        uint64_t from = 0, count = 0;
        std::string file = "hfr_writeset.txt";
    };
    const Config s_config = [] {
        Config c;
        if (const char* v = std::getenv("NFSMW_HFR_WRITESET"))
        {
            char file[512] = {};
            unsigned long long from = 0, count = 0;
            const int n = sscanf(v, "%llu,%llu,%511s", &from, &count, file);
            if (n >= 2)
            {
                c.from = from;
                c.count = count;
                if (n == 3)
                    c.file = file;
                fprintf(stderr, "[hfr] NFSMW_HFR_WRITESET: %llu measurements of guest memory from frame %llu into %s (DIAGNOSTIC)\n",
                    count, from, c.file.c_str());
            }
        }
        return c;
    }();

    struct Region
    {
        uint32_t address, size;
    };
    struct Run
    {
        uint32_t address, size;
    };

    std::vector<Region> CommittedRegions()
    {
        std::vector<Region> out;
        for (vmem::PageHeap* heap : { &vmem::Virtual4K(), &vmem::Virtual64K(), &vmem::Physical() })
        {
            uint32_t a = heap->Base();
            while (a < heap->End())
            {
                vmem::RegionInfo info{};
                if (!heap->Query(a, &info) || !info.regionSize)
                    break;
                if (info.state & vmem::X_MEM_COMMIT)
                    out.push_back({ info.baseAddress, info.regionSize });
                a = info.baseAddress + info.regionSize;
            }
        }
        out.push_back({ 0x82000000u, 0x00CD0000u });  // the XEX image with its data and BSS
        return out;
    }

    struct Snapshot
    {
        std::vector<Region> regions;
        std::vector<std::vector<uint8_t>> bytes;
        void Take(uint8_t* base)
        {
            regions = CommittedRegions();
            bytes.resize(regions.size());
            for (size_t i = 0; i < regions.size(); i++)
                bytes[i].assign(base + regions[i].address, base + regions[i].address + regions[i].size);
        }
        // Runs of bytes that differ now (gaps under 64 bytes merged).
        std::vector<Run> Changed(uint8_t* base) const
        {
            std::vector<Run> runs;
            for (size_t i = 0; i < regions.size(); i++)
            {
                const uint8_t* now = base + regions[i].address;
                const uint8_t* was = bytes[i].data();
                const uint32_t size = regions[i].size;
                uint32_t k = 0;
                while (k < size)
                {
                    // Fast skip of equal 8-byte words.
                    while (k + 8 <= size && !memcmp(now + k, was + k, 8))
                        k += 8;
                    while (k < size && now[k] == was[k])
                        k++;
                    if (k >= size)
                        break;
                    const uint32_t start = k;
                    uint32_t end = k + 1, quiet = 0;
                    for (k++; k < size && quiet < 64; k++)
                    {
                        if (now[k] != was[k])
                        {
                            end = k + 1;
                            quiet = 0;
                        }
                        else
                            quiet++;
                    }
                    runs.push_back({ regions[i].address + start, end - start });
                    k = end;
                }
            }
            return runs;
        }
    };

    uint64_t Bytes(const std::vector<Run>& runs)
    {
        uint64_t n = 0;
        for (const Run& r : runs)
            n += r.size;
        return n;
    }
    bool Overlaps(const std::vector<Run>& sorted, const Run& r)
    {
        auto it = std::upper_bound(sorted.begin(), sorted.end(), r.address, [](uint32_t a, const Run& x) { return a < x.address; });
        if (it != sorted.begin())
        {
            auto p = std::prev(it);
            if (p->address + p->size > r.address)
                return true;
        }
        return it != sorted.end() && it->address < r.address + r.size;
    }
    std::string Where(uint32_t address)
    {
        if (address >= 0x82000000u && address < 0x82CD0000u)
            return "xex";
        vmem::PageHeap* heap = vmem::HeapFor(address);
        if (!heap)
            return "?";
        vmem::RegionInfo info{};
        heap->Query(address, &info);
        char s[64];
        snprintf(s, sizeof(s), "%s alloc %08X", heap == &vmem::Physical() ? "phys" : heap == &vmem::Virtual64K() ? "v64k" : "v4k",
            info.allocationBase);
        return s;
    }

    // Main loop thread only.
    Snapshot s_snapshot;
    // The measurement under way: its phase (1-3), and the frame it may go on
    // from (the copies take long enough that the next frames catch up with
    // two world steps, and draw no in-between picture: 60 frames between
    // measurements, and phase 1 waits for a frame that draws one).
    int s_phase = 1;
    uint64_t s_nextFrame = 0;
    bool s_betweenTaken = false;
    uint32_t s_wait = 0;  // frames to let pass before the next measurement
    std::vector<Run> s_between, s_real, s_control, s_noise;
    std::vector<std::pair<Run, std::vector<uint8_t>>> s_betweenBytes;  // B's runs as the in-between left them
    FILE* s_out = nullptr;
    uint64_t s_done = 0;

    void Write(const char* what, const std::vector<Run>& runs, uint8_t* base)
    {
        fprintf(s_out, "%s: %zu runs, %llu bytes\n", what, runs.size(), (unsigned long long)Bytes(runs));
        for (const Run& r : runs)
        {
            fprintf(s_out, "  %08X +%u %s |", r.address, r.size, Where(r.address).c_str());
            for (uint32_t k = 0; k < std::min<uint32_t>(r.size, 16); k++)
                fprintf(s_out, " %02X", base[r.address + k]);
            fprintf(s_out, "\n");
        }
    }

    void Summarize(uint8_t* base, uint64_t measurement)
    {
        auto sorted = [](std::vector<Run> v) { std::sort(v.begin(), v.end(), [](const Run& a, const Run& b) { return a.address < b.address; }); return v; };
        const std::vector<Run> control = sorted(s_control), noise = sorted(s_noise), real = sorted(s_real);
        std::vector<Run> suspects;
        uint64_t rewritten = 0;
        for (const auto& [r, bytes] : s_betweenBytes)
        {
            if (memcmp(base + r.address, bytes.data(), r.size))
                rewritten += r.size;
            if (!Overlaps(control, r) && !Overlaps(noise, r))
                suspects.push_back(r);
        }
        fprintf(stderr, "[hfr] write set %llu: the in-between picture left %zu runs (%llu bytes) changed, %llu bytes written again by the real "
                        "one; a real render changes %zu runs (%llu bytes), the other threads %zu (%llu bytes); %zu runs (%llu bytes) the "
                        "in-between picture changed that neither does (leak candidates, in %s)\n",
            (unsigned long long)measurement, s_between.size(), (unsigned long long)Bytes(s_between), (unsigned long long)rewritten,
            s_control.size(), (unsigned long long)Bytes(s_control), s_noise.size(), (unsigned long long)Bytes(s_noise), suspects.size(),
            (unsigned long long)Bytes(suspects), s_config.file.c_str());
        fprintf(s_out, "=== measurement %llu\n", (unsigned long long)measurement);
        Write("LEAK CANDIDATES (in-between left changed; no real render or other thread touches)", suspects, base);
        Write("B: in-between left changed", s_between, base);
        Write("R: changed since B, through the real picture", s_real, base);
        Write("C: a real render (control frame)", s_control, base);
        Write("N: other threads, 10 ms", s_noise, base);
        fflush(s_out);
    }
}

namespace hfrdiag
{
    // Main loop thread, from game/hfr.cpp's render entry: the phase this
    // frame plays in a measurement (0: none). 1: snapshot before the
    // in-between picture; 2: a control frame (no in-between picture); 3: the
    // noise frame.
    int WriteSetPhase(uint64_t frame)
    {
        if (!s_config.count || frame < s_config.from || s_done >= s_config.count)
            return 0;
        if (s_wait)
        {
            s_wait--;
            return 0;
        }
        return s_phase;
    }

    void WriteSetBeforeBetween(uint8_t* base)
    {
        if (!s_out && !(s_out = fopen(s_config.file.c_str(), "w")))
            return;
        s_snapshot.Take(base);
        s_betweenTaken = true;
    }
    void WriteSetAfterBetween(uint8_t* base)
    {
        if (!s_out)
            return;
        s_between = s_snapshot.Changed(base);
        s_betweenBytes.clear();
        for (const Run& r : s_between)
            s_betweenBytes.push_back({ r, std::vector<uint8_t>(base + r.address, base + r.address + r.size) });
    }
    void WriteSetAfterReal(uint8_t* base)
    {
        if (!s_out || !s_betweenTaken)
            return;  // no in-between picture this frame: phase 1 again next frame
        s_betweenTaken = false;
        s_real = s_snapshot.Changed(base);
        s_phase = 2;
        s_nextFrame = 0;
    }
    void WriteSetControlBefore(uint8_t* base)
    {
        if (s_out)
            s_snapshot.Take(base);
    }
    void WriteSetControlAfter(uint8_t* base)
    {
        if (!s_out)
            return;
        s_control = s_snapshot.Changed(base);
        s_phase = 3;
    }
    void WriteSetNoise(uint8_t* base)
    {
        if (!s_out)
            return;
        s_snapshot.Take(base);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        s_noise = s_snapshot.Changed(base);
        Summarize(base, ++s_done);
        s_phase = 1;
        s_nextFrame = 0;  // set by the caller's frame count: WriteSetPhase sees it next frame
        s_wait = 60;
    }
}
