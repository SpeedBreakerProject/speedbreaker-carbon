// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// NFSMW_FRAME_TAP=<path>,<ms>[,<x0>,<y0>,<x1>,<y1>]...: a test tool. Every <ms>
// milliseconds, part of the game's picture (fractions of it, HUD included,
// our UI not; the whole picture by default) is written to <path> as a binary
// PPM, through <path>.tmp and a rename, so a reader never sees half a file.
// A comment line in the header carries the frame's sequence number and the
// time in ms on NFSMW_INPUT_SCRIPT's clock (guest time since launch). With
// NFSMW_INPUT_FILE (hid/input.h) a program outside can watch the game and
// drive it: a closed loop for automated runs that a fixed
// NFSMW_INPUT_SCRIPT can't do (a race through bends).
// A file named <path>.snap (any content) asks for the whole picture once: it
// is written to <path>.full (the same PPM header) and the request removed,
// so a watcher can see a menu or a notice without a whole-picture tap.
#include <stdafx.h>
#include "frame_tap.h"
#include <hid/input.h>
#include <video/presenter.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace debug
{
    void StartFrameTap()
    {
        const char* v = std::getenv("NFSMW_FRAME_TAP");
        if (!v || !*v)
            return;
        // <path>,<ms>[,<x0>,<y0>,<x1>,<y1>]...: more rectangles go to
        // <path>.1, <path>.2, ... (the same sequence number in each).
        std::string spec = v;
        std::vector<std::string> fields;
        for (size_t pos = 0;;)
        {
            size_t next = spec.find(',', pos);
            fields.push_back(spec.substr(pos, next == std::string::npos ? std::string::npos : next - pos));
            if (next == std::string::npos)
                break;
            pos = next + 1;
        }
        const std::string path = fields[0];
        const int ms = std::max(10, fields.size() > 1 ? std::atoi(fields[1].c_str()) : 100);
        struct Rect { float x0, y0, x1, y1; std::string path; };
        std::vector<Rect> rects;
        for (size_t i = 2; i + 3 < fields.size(); i += 4)
            rects.push_back({ float(std::atof(fields[i].c_str())), float(std::atof(fields[i + 1].c_str())),
                float(std::atof(fields[i + 2].c_str())), float(std::atof(fields[i + 3].c_str())),
                rects.empty() ? path : path + "." + std::to_string(rects.size()) });
        if (rects.empty())
            rects.push_back({ 0, 0, 1, 1, path });
        for (const Rect& r : rects)
            fprintf(stderr, "[tap] every %d ms: (%.3f, %.3f)-(%.3f, %.3f) of the picture to %s\n", ms, r.x0, r.y0, r.x1, r.y1,
                r.path.c_str());
        std::thread([rects, ms, path] {
            const std::string snapRequest = path + ".snap";
            uint64_t seq = 0;
            auto next = std::chrono::steady_clock::now();
            for (;;)
            {
                next += std::chrono::milliseconds(ms);
                std::vector<std::future<video::CapturedFrame>> futures;
                for (const Rect& r : rects)
                    futures.push_back(video::CaptureRegion(r.x0, r.y0, r.x1, r.y1));
                std::error_code ec;
                const bool snap = std::filesystem::exists(snapRequest, ec);
                std::vector<Rect> all = rects;
                if (snap)
                {
                    futures.push_back(video::CaptureRegion(0, 0, 1, 1));
                    all.push_back({ 0, 0, 1, 1, path + ".full" });
                }
                const long long t = (long long)(hid::ScriptSeconds() * 1000.0);
                seq++;
                for (size_t i = 0; i < all.size(); i++)
                {
                    video::CapturedFrame frame;
                    if (futures[i].wait_for(std::chrono::seconds(2)) == std::future_status::ready)
                    {
                        try
                        {
                            frame = futures[i].get();
                        }
                        catch (const std::future_error&)
                        {
                        }
                    }
                    if (!frame.width || !frame.height)
                        continue;
                    const std::string tmp = all[i].path + ".tmp";
                    if (FILE* out = fopen(tmp.c_str(), "wb"))
                    {
                        fprintf(out, "P6\n# seq %llu t %lld\n%u %u\n255\n", (unsigned long long)seq, t, frame.width, frame.height);
                        fwrite(frame.rgb.data(), 1, frame.rgb.size(), out);
                        fclose(out);
                        std::rename(tmp.c_str(), all[i].path.c_str());
                    }
                }
                if (snap)
                    std::filesystem::remove(snapRequest, ec);
                std::this_thread::sleep_until(next);
                if (std::chrono::steady_clock::now() > next + std::chrono::milliseconds(ms))
                    next = std::chrono::steady_clock::now();
            }
        }).detach();
    }
}
