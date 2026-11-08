// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// A guest thread's APC queue, as Xenia's XThread keeps one (EnqueueApc,
// DeliverAPCs, Enter/LeaveCriticalRegion in kernel/xthread.cc). An XAM
// request's XOVERLAPPED completion routine is queued on the thread that made
// the request and runs when that thread next waits alertably, sleeps
// alertably or leaves its last critical region; never inside the request
// itself (speedbreaker-carbon #7: XUserWriteAchievements' routine).
// Header only (tests/apc_queue_test.cpp); kernel/overlapped.cpp runs them.
#pragma once
#include <cstdint>
#include <deque>

namespace apc
{
    // The routine is called as routine(context, arg1, arg2): for a
    // completion routine (result, length, overlapped), as Xenia queues it.
    struct Apc
    {
        uint32_t routine, context, arg1, arg2;
    };

    class Queue
    {
    public:
        void Enqueue(const Apc& apc) { m_items.push_back(apc); }
        size_t Pending() const { return m_items.size(); }

        // Runs the queued routines, oldest first, unless a critical region
        // holds them back. Each leaves the queue before it runs, so one that
        // queues another or waits alertably itself never runs twice. Returns
        // how many ran.
        template<typename Run>
        uint32_t Deliver(Run&& run)
        {
            uint32_t ran = 0;
            while (m_disable == 0 && !m_items.empty())
            {
                Apc apc = m_items.front();
                m_items.pop_front();
                run(apc);
                ran++;
            }
            return ran;
        }

        // KeEnterCriticalRegion / KeLeaveCriticalRegion (nested). True when
        // the last one is left: the time to deliver what waited.
        void EnterCriticalRegion() { m_disable++; }
        bool LeaveCriticalRegion() { return m_disable != 0 && --m_disable == 0; }

    private:
        std::deque<Apc> m_items;
        uint32_t m_disable = 0;
    };
}
