// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// The options a game's build doesn't offer (user/settings.cpp, linked as the
// game links it). Built twice by ctest: settings_offered_test (Most Wanted:
// Cooler Colors and Shadow Resolution shown, saved and applied as before) and
// settings_offered_carbon_test (-DSB_GAME_CARBON: both hidden and forced to
// their defaults, Off and 1x, whatever settings.toml or the environment
// hold; the stored values are kept and saved as they were). The arguments: a
// scratch folder (settings.toml is written there), then renderer.cpp,
// presenter.cpp and system_info.cpp, read to check that Carbon's renderer
// keeps the shadow map at 1x without the live change, its presenter keeps
// Cooler Colors off, and the [system] settings dump names a forced option.
//
// Build by hand (from the repo root; add -DSB_GAME_CARBON=1 for Carbon's):
//   c++ -std=c++20 -O2 -UNDEBUG -I runtime tests/settings_offered_test.cpp runtime/user/settings.cpp runtime/platform/posix/file_io.cpp -o build/settings_offered_test
//   build/settings_offered_test build/settings_offered_scratch runtime/gpu/renderer.cpp runtime/video/presenter.cpp runtime/report/system_info.cpp
#include <user/settings.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#ifdef _WIN32
static int setenv(const char* name, const char* value, int) { return _putenv_s(name, value); }
static int unsetenv(const char* name) { return _putenv_s(name, ""); }
#endif

namespace
{
    int s_failures = 0;

#define CHECK(cond, ...)                                                    \
    do                                                                      \
    {                                                                       \
        if (!(cond))                                                        \
        {                                                                   \
            s_failures++;                                                   \
            fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); \
            fprintf(stderr, __VA_ARGS__);                                   \
            fprintf(stderr, "\n");                                          \
        }                                                                   \
    } while (0)

    using settings::Id;

#ifdef SB_GAME_CARBON
    constexpr bool kCarbon = true;
#else
    constexpr bool kCarbon = false;
#endif

    std::string Read(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    void Write(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream(path, std::ios::binary) << text;
    }

    bool Has(const std::string& text, std::string_view what)
    {
        return text.find(what) != std::string::npos;
    }

    // The text between `from` and the next `to` after it ("" if either is missing).
    std::string Between(const std::string& text, std::string_view from, std::string_view to)
    {
        const size_t a = text.find(from);
        if (a == std::string::npos)
            return {};
        const size_t b = text.find(to, a + from.size());
        return b == std::string::npos ? std::string() : text.substr(a, b - a);
    }
}

int main(int argc, char** argv)
{
    unsetenv("NFSMW_COOLER_COLORS");
    unsetenv("NFSMW_SHADOW_RESOLUTION");
    const std::filesystem::path dir = argc > 1 ? argv[1] : "settings_offered_scratch";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / "settings.toml";

    const settings::Option& cooler = settings::Info(Id::CoolerColors);
    const settings::Option& shadows = settings::Info(Id::ShadowResolution);
    CHECK(cooler.hidden == kCarbon && cooler.forced == kCarbon, "Cooler Colors: hidden %d, forced %d", cooler.hidden, cooler.forced);
    CHECK(shadows.hidden == kCarbon && shadows.forced == kCarbon, "Shadow Resolution: hidden %d, forced %d", shadows.hidden, shadows.forced);
    // Only those two are forced (the other hidden rows, the window's size and
    // the wheel's calibration, are this build's own state).
    for (const settings::Option& o : settings::Options())
        if (o.id != Id::CoolerColors && o.id != Id::ShadowResolution)
            CHECK(!o.forced, "%s.%s forced", o.table, o.key);
    CHECK(settings::DefaultValue(Id::CoolerColors) == 0, "Cooler Colors' default is Off");
    CHECK(settings::DefaultValue(Id::ShadowResolution) == settings::kShadowResolutionDefault, "Shadow Resolution's default is 1x");

    // A settings.toml with both on (a Most Wanted file copied over, or a hand
    // edit): Most Wanted applies them, Carbon keeps Off and 1x but saves the
    // file's values back unchanged.
    Write(file, "[video]\ncooler_colors = true\nshadow_resolution = \"2x\"\n");
    CHECK(settings::Load(file), "load: %s", settings::LastError().c_str());
    CHECK(settings::GetStoredValue(Id::CoolerColors) == 1 && settings::GetStoredValue(Id::ShadowResolution) == 4, "the file's values stored");
    if (kCarbon)
    {
        CHECK(!settings::GetBool(Id::CoolerColors), "Carbon: Cooler Colors forced off");
        CHECK(settings::GetInt(Id::ShadowResolution) == settings::kShadowResolutionDefault, "Carbon: Shadow Resolution forced to 1x: %d",
            settings::GetInt(Id::ShadowResolution));
    }
    else
    {
        CHECK(settings::GetBool(Id::CoolerColors), "Most Wanted: Cooler Colors on from the file");
        CHECK(settings::GetInt(Id::ShadowResolution) == 4, "Most Wanted: Shadow Resolution 2x from the file: %d", settings::GetInt(Id::ShadowResolution));
    }
    CHECK(settings::Save(file), "save: %s", settings::LastError().c_str());
    const std::string saved = Read(file);
    CHECK(Has(saved, "cooler_colors = true") && Has(saved, "shadow_resolution = \"2x\""), "the stored values saved as they were:\n%s", saved.c_str());

    // Set (as a menu would): stored; Carbon's run keeps the defaults.
    const settings::SetResult set = settings::Set(Id::ShadowResolution, 3);
    CHECK(set == (kCarbon ? settings::SetResult::Overridden : settings::SetResult::Applied), "set 1.5x: %d", int(set));
    CHECK(settings::GetInt(Id::ShadowResolution) == (kCarbon ? settings::kShadowResolutionDefault : 3), "after set: %d",
        settings::GetInt(Id::ShadowResolution));
    CHECK(settings::Set(Id::CoolerColors, 0) == (kCarbon ? settings::SetResult::Overridden : settings::SetResult::Applied), "set Cooler Colors off");
    CHECK(settings::Set(Id::CoolerColors, 1) == (kCarbon ? settings::SetResult::Overridden : settings::SetResult::Applied), "set Cooler Colors on");
    CHECK(settings::GetBool(Id::CoolerColors) == !kCarbon, "Cooler Colors after set: %d", settings::GetBool(Id::CoolerColors));

    // The environment: Most Wanted's override for one run; ignored by Carbon
    // (and not reported as an override, so the dump doesn't say "from").
    setenv("NFSMW_COOLER_COLORS", "1", 1);
    setenv("NFSMW_SHADOW_RESOLUTION", "2x", 1);
    Write(file, "[video]\ncooler_colors = false\nshadow_resolution = \"1x\"\n");
    CHECK(settings::Load(file), "load with the environment: %s", settings::LastError().c_str());
    CHECK(settings::GetBool(Id::CoolerColors) == !kCarbon, "NFSMW_COOLER_COLORS=1: %d", settings::GetBool(Id::CoolerColors));
    CHECK(settings::GetInt(Id::ShadowResolution) == (kCarbon ? settings::kShadowResolutionDefault : 4), "NFSMW_SHADOW_RESOLUTION=2x: %d",
        settings::GetInt(Id::ShadowResolution));
    CHECK((settings::EnvOverride(Id::CoolerColors) != nullptr) == !kCarbon, "Cooler Colors' override reported: %d",
        settings::EnvOverride(Id::CoolerColors) != nullptr);
    CHECK((settings::EnvOverride(Id::ShadowResolution) != nullptr) == !kCarbon, "Shadow Resolution's override reported: %d",
        settings::EnvOverride(Id::ShadowResolution) != nullptr);
    unsetenv("NFSMW_COOLER_COLORS");
    unsetenv("NFSMW_SHADOW_RESOLUTION");

    // The consumers (read, both builds): Carbon's renderer keeps 1x and never
    // makes the live change; its presenter keeps Cooler Colors off; the dump
    // names a forced option instead of its file or environment value.
    if (argc > 4)
    {
        const std::string renderer = Read(argv[2]), presenter = Read(argv[3]), info = Read(argv[4]);
        CHECK(!renderer.empty() && !presenter.empty() && !info.empty(), "sources read");
        const std::string init = Between(renderer, "#ifdef SB_GAME_CARBON\n            // Carbon has no 1600x1600 sun shadow map", "#endif");
        CHECK(Has(init, "s_shadowResolutionSetting = settings::kShadowResolutionDefault;"), "renderer.cpp: Carbon starts at 1x");
        const std::string apply = Between(renderer, "#ifndef SB_GAME_CARBON\n            const int32_t shadowSetting", "#endif");
        CHECK(Has(apply, "ApplyShadowResolution(shadowSetting);"), "renderer.cpp: the live change only outside Carbon");
        CHECK(Has(renderer, "#ifndef SB_GAME_CARBON\n        // Not in Carbon: no pitch-1600 depth target to remake there"),
            "renderer.cpp: ApplyShadowResolution not built for Carbon");
        const std::string tone = Between(presenter, "#ifdef SB_GAME_CARBON\n            const bool coolerColors = false;", "#endif");
        CHECK(!tone.empty(), "presenter.cpp: Carbon's tone keeps Cooler Colors off");
        CHECK(Has(info, "if (o.forced)") && Has(info, "not offered in this build"), "system_info.cpp: the dump names forced options");
    }
    else
        CHECK(false, "usage: settings_offered_test <scratch> <renderer.cpp> <presenter.cpp> <system_info.cpp>");

    std::filesystem::remove_all(dir);
    if (s_failures)
    {
        fprintf(stderr, "%d failure(s)\n", s_failures);
        return 1;
    }
    printf("settings_offered_test (%s): ok\n", kCarbon ? "Carbon" : "Most Wanted");
    return 0;
}
