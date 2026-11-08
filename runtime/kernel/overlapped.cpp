// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include <stdafx.h>
#include "overlapped.h"
#include "dispatcher.h"
#include "function.h"

#include <cpu/guest_thread.h>

namespace
{
    thread_local apc::Queue t_apcs;
}

apc::Queue& ThreadApcs()
{
    return t_apcs;
}

uint32_t DeliverApcs(const char* where)
{
    if (t_apcs.Pending() == 0)
        return 0;
    return t_apcs.Deliver([where](const apc::Apc& a)
    {
        fprintf(stderr, "[apc] running %08X(%08X, %08X, %08X) on thread %08X (%s)\n", a.routine, a.context, a.arg1,
            a.arg2, GuestThread::GetCurrentThreadId(), where);
        GuestToHostFunction<void>(a.routine, a.context, a.arg1, a.arg2);
    });
}

void CompleteOverlapped(XXOVERLAPPED* overlapped, uint32_t result, uint32_t extendedError, uint32_t length)
{
    overlapped->Error = result;
    overlapped->dwExtendedError = extendedError;
    overlapped->Length = length;
    uint32_t event = overlapped->hEvent;
    if (event != 0 && IsKernelObject(event))
        if (auto* e = dynamic_cast<EventObject*>(GetKernelObject(event)))
            e->Set();
    if (uint32_t routine = overlapped->pCompletionRoutine; routine != 0)
    {
        // Xenia: thread->EnqueueApc(routine, result, length, overlapped).
        uint32_t guest = g_memory.MapVirtual(overlapped);
        t_apcs.Enqueue({ routine, result, length, guest });
        fprintf(stderr, "[apc] completion routine %08X queued on thread %08X (overlapped %08X, result %08X)\n", routine,
            GuestThread::GetCurrentThreadId(), guest, result);
    }
}
