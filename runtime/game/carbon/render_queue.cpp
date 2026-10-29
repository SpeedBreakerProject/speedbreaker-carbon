// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// NFS: Carbon's deferred render-callback queue (state at 0x82B8C764) is Most
// Wanted's (runtime/game/render_queue.cpp): a lock-free single-producer,
// single-consumer list that a loading or cutscene thread fills with render
// callbacks (sub_82469318, and the same code inlined in sub_824EDFE0 three
// times, sub_824EF608 and sub_824EF700) and the main thread plays back
// (sub_82469390). It has no barriers and relies on the entry's stores
// becoming visible before the write pointer and count that publish it: true
// on x86 and the 360, not on ARM64, where Most Wanted's copy called a null
// callback in about half the runs. Mid-asm hooks (config/carbon/carbon.toml)
// add a release fence before each publish and an acquire fence before the
// consumer reads an entry.
#include <stdafx.h>
#include <atomic>

void RenderQueuePublish()
{
    std::atomic_thread_fence(std::memory_order_release);
}

void RenderQueueAcquire()
{
    std::atomic_thread_fence(std::memory_order_acquire);
}
