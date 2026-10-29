// Tests for the per-user folders' names (runtime/user/paths.h): each game has
// its own data and cache folders, so a Carbon build never reads or writes Most
// Wanted's settings, saves or installed game (on Linux: ~/.local/share/
// speedbreaker-carbon beside ~/.local/share/speedbreaker). Built twice by
// ctest: user_folders_test (Most Wanted) and user_folders_carbon_test
// (-DSB_GAME_CARBON). Given the SteamOS launcher (scripts/steamos/bundle/
// speedbreaker.sh) and make_bundle.sh (ctest passes both), it also checks
// their copies of the Linux name: the launcher's DATA_NAME (where its log
// goes when its folder isn't writable) and the one make_bundle.sh writes into
// each game's bundle, and that the games' bundle folders differ. From the
// repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/user_folders_test.cpp -o build/user_folders_test
//   clang++ -std=c++20 -O2 -Iruntime -DSB_GAME_CARBON tests/user_folders_test.cpp -o build/user_folders_carbon_test
//   build/user_folders_test scripts/steamos/bundle/speedbreaker.sh scripts/steamos/make_bundle.sh
#include <user/paths.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace
{
    // The names as players and the READMEs know them, per game and platform.
    constexpr const char* kMwLinux = "speedbreaker";
    constexpr const char* kMwApple = "SpeedBreaker";
    constexpr const char* kCarbonLinux = "speedbreaker-carbon";
    constexpr const char* kCarbonApple = "SpeedBreaker Carbon";
#ifdef SB_GAME_CARBON
    constexpr const char* kGame = "carbon";
    constexpr const char* kLinux = kCarbonLinux;
    constexpr const char* kApple = kCarbonApple;
    constexpr const char* kOtherLinux = kMwLinux;
    constexpr const char* kOtherApple = kMwApple;
#else
    constexpr const char* kGame = "mw";
    constexpr const char* kLinux = kMwLinux;
    constexpr const char* kApple = kMwApple;
    constexpr const char* kOtherLinux = kCarbonLinux;
    constexpr const char* kOtherApple = kCarbonApple;
#endif
#if defined(__APPLE__) || defined(_WIN32)
    // Windows' folders are named as macOS's ("SpeedBreaker Carbon").
    constexpr const char* kHere = kApple;
    constexpr const char* kOtherHere = kOtherApple;
#else
    constexpr const char* kHere = kLinux;
    constexpr const char* kOtherHere = kOtherLinux;
#endif

    std::vector<std::string> Lines(const char* path)
    {
        std::vector<std::string> out;
        std::ifstream f(path);
        for (std::string line; std::getline(f, line);)
            out.push_back(line);
        return out;
    }

    void Env(const char* name, const char* value)
    {
#ifdef _WIN32
        _putenv_s(name, value ? value : "");  // "" removes it
#else
        if (value)
            setenv(name, value, 1);
        else
            unsetenv(name);
#endif
    }

    // strcasecmp's question in ASCII, the same everywhere (Windows has no <strings.h>).
    bool SameIgnoringCase(const char* a, const char* b)
    {
        for (; *a && *b; a++, b++)
            if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
                return false;
        return *a == *b;
    }

    void TestNames()
    {
        CHECK(std::strcmp(kUserFolderName, kHere) == 0, "kUserFolderName \"%s\", want \"%s\"", kUserFolderName, kHere);
        CHECK(std::strcmp(kUserFolderName, kOtherHere) != 0, "the other game's name \"%s\"", kOtherHere);
        CHECK(std::strcmp(DataFolderName(), kHere) == 0, "DataFolderName \"%s\"", DataFolderName());
        CHECK(std::strcmp(CacheFolderName(), kHere) == 0, "CacheFolderName \"%s\"", CacheFolderName());
        // Neither game's name is the other's with something cut off or added
        // in a way a case-insensitive file system would merge.
        CHECK(!SameIgnoringCase(kMwLinux, kCarbonLinux) && !SameIgnoringCase(kMwApple, kCarbonApple), "names collide");
    }

    void TestPaths()
    {
        Env("HOME", "/tmp/uf-home");
        Env("XDG_DATA_HOME", nullptr);
        Env("XDG_CACHE_HOME", nullptr);
        std::string name = kHere;
#if defined(_WIN32)
        // Test runs' XDG_* (a player's: %LOCALAPPDATA%, paths_test's).
        Env("XDG_DATA_HOME", "C:\\uf-xdg\\data");
        Env("XDG_CACHE_HOME", "C:\\uf-xdg\\cache");
        CHECK(HomeUserPath() == std::filesystem::path("C:\\uf-xdg\\data") / name, "%s", HomeUserPath().string().c_str());
        CHECK(HomeCachePath() == std::filesystem::path("C:\\uf-xdg\\cache") / name, "%s", HomeCachePath().string().c_str());
        CHECK(HomeUserPath().filename() != kOtherApple, "%s", HomeUserPath().string().c_str());
        Env("XDG_DATA_HOME", nullptr);
        Env("XDG_CACHE_HOME", nullptr);
#elif defined(__APPLE__)
        CHECK(HomeUserPath() == "/tmp/uf-home/Library/Application Support/" + name, "%s", HomeUserPath().c_str());
        CHECK(HomeCachePath() == "/tmp/uf-home/Library/Caches/" + name, "%s", HomeCachePath().c_str());
#else
        CHECK(HomeUserPath() == "/tmp/uf-home/.local/share/" + name, "%s", HomeUserPath().c_str());
        CHECK(HomeCachePath() == "/tmp/uf-home/.cache/" + name, "%s", HomeCachePath().c_str());
        Env("XDG_DATA_HOME", "/tmp/uf-xdg/data");
        Env("XDG_CACHE_HOME", "/tmp/uf-xdg/cache");
        CHECK(HomeUserPath() == "/tmp/uf-xdg/data/" + name, "%s", HomeUserPath().c_str());
        CHECK(HomeCachePath() == "/tmp/uf-xdg/cache/" + name, "%s", HomeCachePath().c_str());
        CHECK(HomeUserPath().filename() != kOtherLinux, "%s", HomeUserPath().c_str());
#endif
    }

    // The launcher's own name is Most Wanted's (the default every bundle
    // starts from), and make_bundle.sh rewrites it per game.
    void TestScripts(const char* launcher, const char* makeBundle)
    {
        std::vector<std::string> data;
        for (const std::string& l : Lines(launcher))
            if (l.rfind("DATA_NAME=", 0) == 0)
                data.push_back(l.substr(10));
        CHECK(data.size() == 1, "%zu DATA_NAME= lines in %s", data.size(), launcher);
        if (data.size() == 1)
            CHECK(data[0] == kMwLinux, "the launcher's DATA_NAME=%s, want %s", data[0].c_str(), kMwLinux);
        bool usesIt = false;
        for (const std::string& l : Lines(launcher))
            usesIt |= l.find("LOG_DIR=\"${XDG_DATA_HOME:-$HOME/.local/share}/$DATA_NAME\"") != std::string::npos;
        CHECK(usesIt, "the launcher's fallback log folder isn't $DATA_NAME");

        // make_bundle.sh: "    <game>) NAME=<folder> ... DATA_NAME=<name> ..." per game.
        std::regex row(R"(^\s+(mw|carbon)\) NAME=(\S+) .*DATA_NAME=(\S+))");
        std::string folders[2], names[2];
        bool rewrites = false;
        for (const std::string& l : Lines(makeBundle))
        {
            std::smatch m;
            if (std::regex_search(l, m, row))
            {
                int i = m[1] == "mw" ? 0 : 1;
                folders[i] = m[2];
                names[i] = m[3];
            }
            rewrites |= l.find("s/^DATA_NAME=speedbreaker\\$/DATA_NAME=$DATA_NAME/") != std::string::npos;
        }
        CHECK(names[0] == kMwLinux, "make_bundle.sh's mw DATA_NAME \"%s\"", names[0].c_str());
        CHECK(names[1] == kCarbonLinux, "make_bundle.sh's carbon DATA_NAME \"%s\"", names[1].c_str());
        CHECK(names[std::strcmp(kGame, "mw") == 0 ? 0 : 1] == kLinux, "this game's (%s) bundle name", kGame);
        CHECK(!folders[0].empty() && !folders[1].empty() && folders[0] != folders[1],
            "bundle folders \"%s\" and \"%s\"", folders[0].c_str(), folders[1].c_str());
        CHECK(rewrites, "make_bundle.sh doesn't rewrite the launcher's DATA_NAME line");
    }
}

int main(int argc, char** argv)
{
    printf("user_folders_test (%s)\n", kGame);
    TestNames();
    TestPaths();
    if (argc >= 3)
        TestScripts(argv[1], argv[2]);
    else
        printf("  (no launcher / make_bundle.sh given: their checks skipped)\n");
    if (g_failures)
    {
        printf("%d FAILED\n", g_failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
