// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>
#include <xbox.h>
#include "apc_queue.h"

// Completes an XOVERLAPPED like Xenia's KernelState::CompleteOverlappedEx:
// result, extended error and length, then signals its event. A completion
// routine is queued as an APC on the calling thread (kernel/apc_queue.h), as
// Xenia does: the request's caller is the thread that completes it here.
void CompleteOverlapped(XXOVERLAPPED* overlapped, uint32_t result, uint32_t extendedError = 0, uint32_t length = 0);

// The calling guest thread's APC queue.
apc::Queue& ThreadApcs();
// Runs the calling guest thread's queued APCs (an alertable wait or sleep,
// the last KeLeaveCriticalRegion; `where` names it in the log). Returns how
// many ran.
uint32_t DeliverApcs(const char* where);
