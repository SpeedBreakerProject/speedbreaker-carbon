// Tests for returning released or decommitted guest pages to zero
// (runtime/kernel/vmem.cpp, PageHeap::ResetHostPages and ResetHostRange) on
// hosts whose pages are larger than the heap's: 16 KB on every Apple-silicon
// Mac, iPhone and iPad (and 16 KB Android), 64 KB on some Linux kernels,
// against the 4 KB-page heaps (Virtual4K, the kernel pool). mmap(MAP_FIXED)
// rounds its length up to whole host pages, so remapping a range that ends
// inside a host page zeroes the guest pages after it, another allocation's.
// Only the whole host pages inside the range may be remapped; the head and
// tail are cleared in place.
//
// Runs on any host: the heaps' host page size is injected (the host's own,
// 16 KB and 64 KB where they are a multiple of it), and each runs once with the
// real mmap and once with a model of a kernel at that page size (it records
// every remap, refuses an unaligned one as the kernel does, and rounds the
// length up and zeroes it as the kernel does). From the repo root:
//   clang++ -std=c++20 -O2 -pthread -Iruntime -Iruntime/kernel -Iruntime/thirdparty/o1heap -Ippc
//     -Itools/XenonRecomp/XenonUtils -Itools/XenonRecomp/thirdparty/simde tests/vmem_host_pages_test.cpp
//     runtime/kernel/vmem.cpp -o build/vmem_host_pages_test
//   build/vmem_host_pages_test
// Under a second.
#include <stdafx.h>
#include <kernel/vmem.h>

#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

// What vmem.cpp needs of the runtime's memory: the base (memory.cpp is not
// linked).
Memory::Memory() {}
Memory g_memory;

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

using namespace vmem;

constexpr uint32_t kGuestPage = 0x1000;

// The model kernel: page size s_modelPage, every call recorded.
struct RemapCall { uintptr_t host; size_t bytes; };
static size_t s_modelPage = 0;
static std::vector<RemapCall> s_calls;
static bool s_failRemap = false;  // the remap fails (ResetHostRange must clear in place)

static bool ModelRemap(void* host, size_t bytes)
{
    s_calls.push_back({ uintptr_t(host), bytes });
    if (s_failRemap || uintptr_t(host) % s_modelPage != 0)
        return false;  // EINVAL: MAP_FIXED needs a page-aligned address
    size_t rounded = (bytes + s_modelPage - 1) / s_modelPage * s_modelPage;
    memset(host, 0, rounded);  // what the kernel's fresh zero pages amount to, whole pages
    return true;
}

static bool RealRemap(void* host, size_t bytes)
{
    s_calls.push_back({ uintptr_t(host), bytes });
    if (s_failRemap)
        return false;
    return RemapAnon(host, bytes);
}

static uint8_t* Host(uint32_t guest) { return static_cast<uint8_t*>(g_memory.Translate(guest)); }

static void Fill(uint32_t guest, uint32_t bytes, uint8_t seed)
{
    for (uint32_t i = 0; i < bytes; i++)
        Host(guest)[i] = uint8_t(seed + i * 7 + (i >> 12)) | 1;  // never 0
}

// The first offset whose byte isn't Fill's, or -1.
static int64_t Differs(uint32_t guest, uint32_t bytes, uint8_t seed)
{
    for (uint32_t i = 0; i < bytes; i++)
        if (Host(guest)[i] != (uint8_t(seed + i * 7 + (i >> 12)) | 1))
            return i;
    return -1;
}

static int64_t NonZero(uint32_t guest, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes; i++)
        if (Host(guest)[i] != 0)
            return i;
    return -1;
}

// Every remap covered only whole host pages, and only inside [lo, hi).
static void CheckCalls(const char* what, size_t hostPage, uint32_t lo, uint32_t hi)
{
    for (const RemapCall& c : s_calls)
    {
        CHECK(c.host % hostPage == 0 && c.bytes % hostPage == 0 && c.bytes != 0,
            "%s: remap at host +%zx of %zx bytes isn't whole %zu KB host pages", what,
            size_t(c.host - uintptr_t(g_memory.base)), c.bytes, hostPage / 1024);
        CHECK(c.host >= uintptr_t(Host(lo)) && c.host + c.bytes <= uintptr_t(Host(hi)),
            "%s: remap %zx+%zx outside the range %08X..%08X", what, size_t(c.host - uintptr_t(g_memory.base)),
            c.bytes, lo, hi);
    }
}

// One heap of 4 KB guest pages at a base aligned to every host page size
// tried, with `hostPage` injected.
static void RunHeap(size_t hostPage, RemapFn remap, const char* how)
{
    printf("host pages %zu KB, %s\n", hostPage / 1024, how);
    constexpr uint32_t kBase = 0x00100000, kSize = 0x01000000;
    const uint32_t hp = uint32_t(hostPage / kGuestPage);  // guest pages a host page

    // 1. The reported case. A: one guest page at the start of a host page;
    // B: the next allocation, adjacent, the rest of that host page and beyond.
    // Releasing A must leave B intact.
    {
        PageHeap heap(kBase, kSize, kGuestPage);
        heap.SetHostPagesForTest(hostPage, remap);
        uint32_t a = heap.Alloc(kGuestPage, kGuestPage, X_MEM_RESERVE | X_MEM_COMMIT, 4, false);
        uint32_t b = heap.Alloc((hp + 2) * kGuestPage, kGuestPage, X_MEM_RESERVE | X_MEM_COMMIT, 4, false);
        CHECK(a == kBase && b == kBase + kGuestPage, "allocations at %08X, %08X", a, b);
        Fill(a, kGuestPage, 0x11);
        Fill(b, (hp + 2) * kGuestPage, 0x5A);
        s_calls.clear();
        uint32_t released = 0;
        CHECK(heap.Release(a, &released) && released == kGuestPage, "release A: %X", released);
        int64_t d = Differs(b, (hp + 2) * kGuestPage, 0x5A);
        CHECK(d < 0, "release A (one 4 KB page): B, the next allocation, changed at +%llX (the remap zeroed it)",
            (long long)d);
        CHECK(NonZero(a, kGuestPage) < 0, "release A: A not zero at +%llX", (long long)NonZero(a, kGuestPage));
        CheckCalls("release A", hostPage, a, a + kGuestPage);
        if (hp > 1)
            CHECK(s_calls.empty(), "release A: %zu remap(s) of a range smaller than a host page", s_calls.size());

        // A new allocation in A's place is zero (and B still intact).
        uint32_t a2 = heap.Alloc(kGuestPage, kGuestPage, X_MEM_RESERVE | X_MEM_COMMIT, 4, false);
        CHECK(a2 == a && NonZero(a2, kGuestPage) < 0, "A again at %08X, not zero", a2);
    }

    // 2. Decommit the head of an allocation (aligned start, a length that
    // isn't whole host pages): the rest of it stays committed and intact.
    {
        PageHeap heap(kBase, kSize, kGuestPage);
        heap.SetHostPagesForTest(hostPage, remap);
        uint32_t pages = 3 * hp + 3;
        uint32_t c = heap.Alloc(pages * kGuestPage, kGuestPage, X_MEM_RESERVE | X_MEM_COMMIT, 4, false);
        Fill(c, pages * kGuestPage, 0x23);
        for (uint32_t n : { 1u, hp > 1 ? hp - 1 : 1u, hp + 1 })
        {
            Fill(c, pages * kGuestPage, 0x23);
            s_calls.clear();
            CHECK(heap.Decommit(c, n * kGuestPage), "decommit %u pages", n);
            int64_t d = -1;  // Fill's pattern depends on the offset: compare at the original offsets
            for (uint32_t i = n * kGuestPage; i < pages * kGuestPage && d < 0; i++)
                if (Host(c)[i] != (uint8_t(0x23 + i * 7 + (i >> 12)) | 1))
                    d = i;
            CHECK(d < 0, "decommit the first %u page(s): the committed rest changed at +%llX", n, (long long)d);
            CHECK(NonZero(c, n * kGuestPage) < 0, "decommit %u: not zero", n);
            CheckCalls("decommit", hostPage, c, c + n * kGuestPage);
            CHECK(heap.AllocFixed(c, n * kGuestPage, X_MEM_COMMIT, 4, nullptr), "recommit %u", n);
        }
    }

    // 3. A range starting and ending inside host pages around whole ones:
    // E | F (whole host pages inside, head and tail partial) | G. The whole
    // pages are remapped (lazily zero, as before), the edges cleared in
    // place, E and G intact.
    {
        PageHeap heap(kBase, kSize, kGuestPage);
        heap.SetHostPagesForTest(hostPage, remap);
        uint32_t fPages = hp > 1 ? 3 * hp - 2 : 1;  // from guest page 1 to the 3rd host page's last but one
        uint32_t e = heap.Alloc(kGuestPage, kGuestPage, X_MEM_RESERVE | X_MEM_COMMIT, 4, false);
        uint32_t f = heap.Alloc(fPages * kGuestPage, kGuestPage, X_MEM_RESERVE | X_MEM_COMMIT, 4, false);
        uint32_t g = heap.Alloc(2 * hp * kGuestPage, kGuestPage, X_MEM_RESERVE | X_MEM_COMMIT, 4, false);
        Fill(e, kGuestPage, 0x31);
        Fill(f, fPages * kGuestPage, 0x47);
        Fill(g, 2 * hp * kGuestPage, 0x69);
        s_calls.clear();
        CHECK(heap.Release(f, nullptr), "release F");
        CHECK(Differs(e, kGuestPage, 0x31) < 0, "release F: E (before it) changed");
        int64_t d = Differs(g, 2 * hp * kGuestPage, 0x69);
        CHECK(d < 0, "release F: G (after it) changed at +%llX", (long long)d);
        CHECK(NonZero(f, fPages * kGuestPage) < 0, "release F: F not zero at +%llX", (long long)NonZero(f, fPages * kGuestPage));
        CheckCalls("release F", hostPage, f, f + fPages * kGuestPage);
        if (hp > 1)
        {
            // F covers host pages 0 (from guest page 1), 1 (whole), 2 (all but its last guest page).
            bool one = s_calls.size() == 1 && s_calls[0].host == uintptr_t(Host(kBase)) + hostPage
                && s_calls[0].bytes == hostPage;
            CHECK(one, "release F: expected one remap of the whole host page inside it, got %zu", s_calls.size());
        }
    }

    // 4. A large release (many host pages, both ends partial) between two
    // live allocations, then the remap failing: cleared in place.
    for (bool fail : { false, true })
    {
        PageHeap heap(kBase, kSize, kGuestPage);
        heap.SetHostPagesForTest(hostPage, remap);
        uint32_t big = 64 * hp + 2;
        uint32_t p = heap.Alloc(3 * kGuestPage, kGuestPage, X_MEM_RESERVE | X_MEM_COMMIT, 4, false);
        uint32_t q = heap.Alloc(big * kGuestPage, kGuestPage, X_MEM_RESERVE | X_MEM_COMMIT, 4, false);
        uint32_t r = heap.Alloc(5 * kGuestPage, kGuestPage, X_MEM_RESERVE | X_MEM_COMMIT, 4, false);
        Fill(p, 3 * kGuestPage, 0x13);
        Fill(q, big * kGuestPage, 0x77);
        Fill(r, 5 * kGuestPage, 0x29);
        s_calls.clear();
        s_failRemap = fail;
        CHECK(heap.Release(q, nullptr), "release Q");
        s_failRemap = false;
        CHECK(Differs(p, 3 * kGuestPage, 0x13) < 0, "release Q%s: P changed", fail ? " (remap failing)" : "");
        CHECK(Differs(r, 5 * kGuestPage, 0x29) < 0, "release Q%s: R changed at +%llX", fail ? " (remap failing)" : "",
            (long long)Differs(r, 5 * kGuestPage, 0x29));
        CHECK(NonZero(q, big * kGuestPage) < 0, "release Q%s: Q not zero at +%llX", fail ? " (remap failing)" : "",
            (long long)NonZero(q, big * kGuestPage));
        CheckCalls("release Q", hostPage, q, q + big * kGuestPage);
        CHECK(s_calls.size() == 1, "release Q: %zu remaps, expected 1 (the whole host pages inside)", s_calls.size());
    }

    // 5. Physical memory (0xA0000000 up, mirrored at three windows) is never
    // remapped: cleared in place.
    {
        PageHeap heap(0xA0000000, kSize, kGuestPage);
        heap.SetHostPagesForTest(hostPage, remap);
        uint32_t x = heap.Alloc(8 * hp * kGuestPage, kGuestPage, X_MEM_RESERVE | X_MEM_COMMIT, 4, false);
        uint32_t y = heap.Alloc(kGuestPage, kGuestPage, X_MEM_RESERVE | X_MEM_COMMIT, 4, false);
        Fill(x, 8 * hp * kGuestPage, 0x3C);
        Fill(y, kGuestPage, 0x4D);
        s_calls.clear();
        CHECK(heap.Release(x, nullptr), "release physical X");
        CHECK(s_calls.empty(), "physical: %zu remap(s)", s_calls.size());
        CHECK(NonZero(x, 8 * hp * kGuestPage) < 0, "physical X not zero");
        CHECK(Differs(y, kGuestPage, 0x4D) < 0, "physical Y changed");
    }
}

// ResetHostRange on its own: every split of a range of whole and partial host
// pages remaps exactly the whole ones.
static void RunSplits(size_t hostPage)
{
    uint8_t* area = Host(0x02000000);
    const size_t span = 4 * hostPage;
    for (size_t start = 0; start <= 2 * hostPage; start += kGuestPage)
        for (size_t bytes = kGuestPage; start + bytes <= span; bytes += kGuestPage)
        {
            memset(area, 0xEE, span);
            s_calls.clear();
            ResetHostRange(area + start, bytes, hostPage, ModelRemap);
            size_t lo = (start + hostPage - 1) / hostPage * hostPage, hi = (start + bytes) / hostPage * hostPage;
            bool ok = true;
            for (size_t i = 0; i < span && ok; i++)
                ok = area[i] == ((i >= start && i < start + bytes) ? 0 : 0xEE);
            bool calls = lo < hi ? s_calls.size() == 1 && s_calls[0].host == uintptr_t(area + lo) && s_calls[0].bytes == hi - lo
                                 : s_calls.empty();
            CHECK(ok && calls, "ResetHostRange(+%zx, %zx) on %zu KB pages: bytes %s, %zu remap(s)", start, bytes,
                hostPage / 1024, ok ? "right" : "WRONG", s_calls.size());
        }
}

int main()
{
    size_t real = size_t(sysconf(_SC_PAGESIZE));
    // The guest's 4 GB, as memory.cpp reserves it, aligned to 64 KB.
    size_t reserve = size_t(PPC_MEMORY_SIZE) + 0x10000;
    void* reserved = mmap(nullptr, reserve, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (reserved == MAP_FAILED)
    {
        perror("reserve guest memory");
        return 1;
    }
    g_memory.base = reinterpret_cast<uint8_t*>((uintptr_t(reserved) + 0xFFFF) & ~uintptr_t(0xFFFF));
    printf("this host's pages: %zu KB\n", real / 1024);

    std::vector<size_t> sizes = { real };
    for (size_t s : { size_t(0x4000), size_t(0x10000) })
        if (s > real && s % real == 0)
            sizes.push_back(s);
    for (size_t hostPage : sizes)
    {
        s_modelPage = hostPage;
        RunHeap(hostPage, ModelRemap, "a model kernel");
        RunHeap(hostPage, RealRemap, "the real mmap");
        RunSplits(hostPage);
    }
    // The heaps' own default: this host's page size and the real mmap.
    {
        PageHeap heap(0x00100000, 0x00100000, kGuestPage);
        uint32_t a = heap.Alloc(kGuestPage, kGuestPage, X_MEM_RESERVE | X_MEM_COMMIT, 4, false);
        uint32_t b = heap.Alloc(4 * kGuestPage, kGuestPage, X_MEM_RESERVE | X_MEM_COMMIT, 4, false);
        Fill(b, 4 * kGuestPage, 0x5A);
        heap.Release(a, nullptr);
        CHECK(Differs(b, 4 * kGuestPage, 0x5A) < 0, "default heap: B changed after releasing A");
    }

    printf(g_failures ? "%d failure(s)\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
