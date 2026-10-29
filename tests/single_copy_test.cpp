// One copy of the game per data folder (runtime/user/single_copy.h). The
// other copy is played by another thread: flock (POSIX) and the named
// mutex (Windows) both turn it away as they would another process.
// tests/CMakeLists.txt builds it with the platform's file_io.cpp.
#include <user/single_copy.h>

#include <platform/file_io.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace fs = std::filesystem;

static singlecopy::Claim FromAnotherThread(const fs::path& data)
{
    singlecopy::Claim claim = singlecopy::Claim::Ours;
    std::thread other([&] { claim = singlecopy::ClaimDataFolder(data); });
    other.join();
    return claim;
}

int main(int argc, char** argv)
{
    fs::path root = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "single_copy_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    fs::path carbon = root / "speedbreaker-carbon";
    fs::path mw = root / "speedbreaker";

    printf("1. the first copy claims its data folder (logs/ is made for it)\n");
    CHECK(singlecopy::ClaimDataFolder(carbon) == singlecopy::Claim::Ours, "not claimed");
    CHECK(fs::is_directory(carbon / "logs"), "no logs folder");

    printf("2. a second copy with the same data folder is told another copy is running\n");
    CHECK(FromAnotherThread(carbon) == singlecopy::Claim::OtherCopy, "a second copy wasn't turned away");

    printf("3. the other game (its own data folder) still starts beside it\n");
    CHECK(FromAnotherThread(mw) == singlecopy::Claim::Ours, "Most Wanted's folder was refused");

    printf("4. the game's own installer can still lock the data folder it installs into\n");
    bool busy = true;
    intptr_t install = platform::LockFolder(carbon, busy);
    CHECK(install >= 0 && !busy, "the installer's lock on the data folder: %lld, busy %d", (long long)install, busy);
    platform::UnlockFolder(install);

    printf("5. a data folder that can't be locked leaves the game unguarded, not refused\n");
    fs::path notFolder = root / "a-file";
    std::ofstream(notFolder) << "x";
    CHECK(FromAnotherThread(notFolder) == singlecopy::Claim::Unguarded, "a folder that can't be locked refused the game");

    printf("6. the page names the game and says what to do\n");
    singlecopy::Message m = singlecopy::AlreadyRunning("SpeedBreaker: Carbon");
    CHECK(m.title == "SpeedBreaker: Carbon is already running", "%s", m.title.c_str());
    CHECK(m.paragraphs.size() == 2 && m.paragraphs[1].find("close it") != std::string::npos, "no way out in the text");

    fs::remove_all(root, ec);
    printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
