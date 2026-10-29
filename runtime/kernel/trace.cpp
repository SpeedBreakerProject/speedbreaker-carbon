// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// NFSMW_TRACE=1 logs every import call; NFSMW_TRACE=Xam,XeCrypt,NtCreateFile
// (anything but "1") logs only the imports whose names start with one of
// those comma-separated prefixes (and no GPU interrupts).
#include <stdafx.h>
#include "function.h"
#include <cpu/guest_thread.h>
#include <string>
#include <vector>

namespace
{
    const char* TraceValue()
    {
        static const char* v = std::getenv("NFSMW_TRACE");
        return v;
    }

    bool TraceAll()
    {
        static const bool all = TraceValue() != nullptr && TraceValue()[0] == '1' && TraceValue()[1] == '\0';
        return all;
    }

    bool Wanted(const char* name)
    {
        if (TraceAll())
            return true;
        static const std::vector<std::string> prefixes = [] {
            std::vector<std::string> out;
            std::string s = TraceValue() ? TraceValue() : "";
            size_t start = 0;
            while (start <= s.size())
            {
                size_t comma = s.find(',', start);
                if (comma == std::string::npos)
                    comma = s.size();
                if (comma > start)
                    out.push_back(s.substr(start, comma - start));
                start = comma + 1;
            }
            return out;
        }();
        for (const std::string& p : prefixes)
            if (strncmp(name, p.c_str(), p.size()) == 0)
                return true;
        return false;
    }
}

bool ImportTraceEnabled()
{
    static const bool enabled = TraceValue() != nullptr && TraceValue()[0] != '\0' && TraceValue()[0] != '0';
    return enabled;
}

bool ImportTraceAll()
{
    return TraceAll();
}

void ImportTraceCall(const char* name, const PPCContext& ctx)
{
    if (!Wanted(name))
        return;
    fprintf(stderr, "[trace t%u] %s(%08X, %08X, %08X, %08X) lr=%08X\n", GuestThread::GetCurrentThreadId(), name,
        ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, uint32_t(ctx.lr));
}

void ImportTraceReturn(const char* name, const PPCContext& ctx)
{
    if (!Wanted(name))
        return;
    fprintf(stderr, "[trace t%u]   %s -> %08X\n", GuestThread::GetCurrentThreadId(), name, ctx.r3.u32);
}
