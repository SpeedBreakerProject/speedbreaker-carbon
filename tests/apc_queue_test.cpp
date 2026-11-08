// Tests for a guest thread's APC queue (runtime/kernel/apc_queue.h, header
// only). From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/apc_queue_test.cpp -o build/apc_queue_test
//   build/apc_queue_test
// 1. Routines run oldest first, with (context, arg1, arg2) as queued, and
//    leave the queue (speedbreaker-carbon #7: XUserWriteAchievements'
//    completion routine, (result, length, overlapped)).
// 2. A routine that queues another and waits alertably (delivers again)
//    runs once; the one it queued runs in that nested delivery.
// 3. Critical regions (nested) hold delivery back; leaving the last one
//    reports it is time to deliver, and only that one.
#include <kernel/apc_queue.h>

#include <cstdio>
#include <functional>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void TestOrder()
{
    printf("order and arguments\n");
    apc::Queue q;
    std::vector<apc::Apc> ran;
    q.Enqueue({ 0x824D6648, 0, 0, 0x82C8061C });
    q.Enqueue({ 0x82000000, 1, 2, 3 });
    CHECK(q.Pending() == 2, "%zu pending", q.Pending());
    uint32_t n = q.Deliver([&](const apc::Apc& a) { ran.push_back(a); });
    CHECK(n == 2 && ran.size() == 2, "ran %u", n);
    CHECK(ran[0].routine == 0x824D6648 && ran[0].arg2 == 0x82C8061C, "first %08X", ran[0].routine);
    CHECK(ran[1].context == 1 && ran[1].arg1 == 2 && ran[1].arg2 == 3, "second's arguments");
    CHECK(q.Pending() == 0 && q.Deliver([&](const apc::Apc&) { ran.push_back({}); }) == 0 && ran.size() == 2,
        "a second delivery ran something");
}

static void TestNested()
{
    printf("a routine that queues another and waits alertably\n");
    apc::Queue q;
    std::vector<uint32_t> ran;
    std::function<void(const apc::Apc&)> run = [&](const apc::Apc& a)
    {
        ran.push_back(a.routine);
        if (a.routine == 1)
        {
            q.Enqueue({ 2, 0, 0, 0 });
            q.Deliver(run);  // its alertable wait
        }
    };
    q.Enqueue({ 1, 0, 0, 0 });
    q.Deliver(run);
    CHECK(ran.size() == 2 && ran[0] == 1 && ran[1] == 2, "ran %zu routines", ran.size());
    CHECK(q.Pending() == 0, "%zu left", q.Pending());
}

static void TestCriticalRegions()
{
    printf("critical regions\n");
    apc::Queue q;
    int ran = 0;
    auto run = [&](const apc::Apc&) { ran++; };
    CHECK(!q.LeaveCriticalRegion(), "leaving with none entered");
    q.EnterCriticalRegion();
    q.EnterCriticalRegion();
    q.Enqueue({ 1, 0, 0, 0 });
    CHECK(q.Deliver(run) == 0 && ran == 0, "delivered inside a critical region");
    CHECK(!q.LeaveCriticalRegion(), "the inner one isn't the last");
    CHECK(q.Deliver(run) == 0 && ran == 0, "delivered inside the outer region");
    CHECK(q.LeaveCriticalRegion(), "the outer one is the last");
    CHECK(q.Deliver(run) == 1 && ran == 1, "not delivered after the last region");
}

int main()
{
    TestOrder();
    TestNested();
    TestCriticalRegions();
    if (g_failures)
    {
        printf("%d FAILED\n", g_failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
