// Tests for each game's identity as the player sees it (runtime/game/game_info.h):
// its name, the releases Check for Updates asks for, its bug reports' names,
// its version and its changelog. Built twice by ctest: game_identity_test
// (Most Wanted, exactly SpeedBreaker's as before) and game_identity_carbon_test
// (-DSB_GAME_CARBON: SpeedBreaker: Carbon's own). Given the repository root
// (ctest passes it), it reads the sources that must take these from
// game_info.h, CMakeLists.txt's versions and each game's changelog entry,
// and the Windows download's names (Windows(): the exe, its resources, the
// message boxes' log folder, make_zip.py's names).
#include <game/game_info.h>
#include <install/placement.h>
#include <update/check.h>
#include <user/paths.h>

#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace
{
    std::string Read(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    bool Has(const std::string& text, std::string_view what)
    {
        return text.find(what) != std::string::npos;
    }

    // Markdown's wrapped lines as one: runs of spaces and line breaks -> one space.
    std::string Flat(const std::string& text)
    {
        std::string out;
        for (char c : text)
        {
            const bool space = c == ' ' || c == '\n';
            if (!space || (!out.empty() && out.back() != ' '))
                out += space ? ' ' : c;
        }
        return out;
    }

    // set(<name> x.y.z) in CMakeLists.txt.
    std::string CMakeVersion(const std::string& cmake, const std::string& name)
    {
        std::smatch m;
        const std::regex re("\\nset\\(" + name + " ([0-9]+\\.[0-9]+\\.[0-9]+)\\)");
        return std::regex_search(cmake, m, re) ? m[1].str() : std::string();
    }

    void Names()
    {
#ifdef SB_GAME_CARBON
        CHECK(std::string_view(SB_APP_NAME) == "SpeedBreaker: Carbon", "%s", SB_APP_NAME);
        CHECK(std::string_view(update::kApiUrl) == "https://api.github.com/repos/SpeedBreakerProject/speedbreaker-carbon/releases/latest",
            "%s", update::kApiUrl);
        CHECK(std::string_view(update::kReleasesPage) == "https://github.com/SpeedBreakerProject/speedbreaker-carbon/releases/latest", "%s",
            update::kReleasesPage);
        CHECK(std::string_view(update::kPagePrefix) == "https://github.com/SpeedBreakerProject/speedbreaker-carbon/releases/", "%s",
            update::kPagePrefix);
        CHECK(std::string_view(SB_FILE_PREFIX) == "speedbreaker-carbon", "%s", SB_FILE_PREFIX);
        CHECK(std::string_view(install::kPickedSubfolder) == "SpeedBreaker Carbon Game", "%s", install::kPickedSubfolder);
        CHECK(std::string_view(SB_GAME_SUPPORTED_DISC).find("USA Collector's Edition") != std::string_view::npos, "%s",
            SB_GAME_SUPPORTED_DISC);
        CHECK(std::string_view(SB_GAME_UNSUPPORTED).find("aren't supported yet") != std::string_view::npos, "%s", SB_GAME_UNSUPPORTED);
        CHECK(std::string_view(SB_WINDOWS_EXE) == "SpeedBreaker-Carbon.exe", "%s", SB_WINDOWS_EXE);
        CHECK(std::string_view(SB_WINDOWS_LOGS) == "%LOCALAPPDATA%\\SpeedBreaker Carbon\\logs", "%s", SB_WINDOWS_LOGS);
        CHECK(std::string_view(SB_IOS_APP_NAME) == "SpeedBreaker Carbon", "%s", SB_IOS_APP_NAME);
#else
        // Most Wanted's: SpeedBreaker's, unchanged.
        CHECK(std::string_view(SB_APP_NAME) == "SpeedBreaker", "%s", SB_APP_NAME);
        CHECK(std::string_view(update::kApiUrl) == "https://api.github.com/repos/SpeedBreakerProject/speedbreaker/releases/latest", "%s",
            update::kApiUrl);
        CHECK(std::string_view(update::kReleasesPage) == "https://github.com/SpeedBreakerProject/speedbreaker/releases/latest", "%s",
            update::kReleasesPage);
        CHECK(std::string_view(update::kPagePrefix) == "https://github.com/SpeedBreakerProject/speedbreaker/releases/", "%s",
            update::kPagePrefix);
        CHECK(std::string_view(SB_FILE_PREFIX) == "speedbreaker", "%s", SB_FILE_PREFIX);
        CHECK(std::string_view(install::kPickedSubfolder) == "SpeedBreaker Game", "%s", install::kPickedSubfolder);
        CHECK(std::string_view(SB_GAME_UNSUPPORTED).empty(), "%s", SB_GAME_UNSUPPORTED);
        CHECK(std::string_view(SB_WINDOWS_EXE) == "SpeedBreaker.exe", "%s", SB_WINDOWS_EXE);
        CHECK(std::string_view(SB_WINDOWS_LOGS) == "%LOCALAPPDATA%\\SpeedBreaker\\logs", "%s", SB_WINDOWS_LOGS);
        CHECK(std::string_view(SB_IOS_APP_NAME) == "SpeedBreaker", "%s", SB_IOS_APP_NAME);
#endif
#if defined(__APPLE__) || defined(_WIN32)
        // The message boxes' folder is the data folder (Windows' name = macOS's).
        CHECK(std::string(SB_WINDOWS_LOGS) == std::string("%LOCALAPPDATA%\\") + kUserFolderName + "\\logs", "%s vs %s",
            SB_WINDOWS_LOGS, kUserFolderName);
#endif
    }

    // The places the player reads the name take it from game_info.h.
    void Sources(const std::string& root)
    {
        const std::string rt = root + "/runtime/";
        const std::string menu = Read(rt + "ui/settings_menu.cpp");
        CHECK(Has(menu, "\"Quit \" SB_APP_NAME \"? Progress since your last save will be lost.\""), "settings_menu.cpp's Quit prompt");
        CHECK(!Has(menu, "Quit SpeedBreaker?"), "settings_menu.cpp: no fixed Quit prompt");
        CHECK(Has(menu, "ImGui::TextWrapped(SB_APP_NAME \" %s\", report::BuildString());"), "settings_menu.cpp's version line");
        CHECK(Has(Read(rt + "video/presenter.cpp"), "SDL_CreateWindow(SB_APP_NAME,"), "presenter.cpp's window title");
        CHECK(Has(Read(rt + "ui/installer_screen.cpp"), "TitleText(SB_APP_NAME);"), "installer_screen.cpp's title");
        CHECK(Has(Read(rt + "report/bug_report.cpp"), "std::format(SB_FILE_PREFIX \"-bug-report-v{}-{}{}.zip\""), "bug_report.cpp's name");
        const std::string check = Read(rt + "update/check.cpp");
        CHECK(Has(check, "\"This is the newest release of \" SB_APP_NAME \".\""), "check.cpp's newest line");
        CHECK(!Has(check, "release of SpeedBreaker"), "check.cpp: no fixed name");
        CHECK(Has(Read(rt + "ui/update_dialog.cpp"), "\"Asks GitHub whether a newer \" SB_APP_NAME \" is out."), "update_dialog.cpp");
    }

    // The Windows download: each game's exe name (runtime/CMakeLists.txt),
    // icon, manifest and version strings (speedbreaker.rc), the message
    // boxes and the installer's hint, and make_zip.py's names.
    void Windows(const std::string& root)
    {
        const std::string w = root + "/runtime/platform/win32/";
        const std::string rcmake = Read(root + "/runtime/CMakeLists.txt");
        CHECK(Has(rcmake, "set(SB_WINDOWS_EXE_NAME SpeedBreaker-Carbon)\n        set_target_properties(SpeedBreaker PROPERTIES OUTPUT_NAME ${SB_WINDOWS_EXE_NAME})"),
            "runtime/CMakeLists.txt names Carbon's exe SpeedBreaker-Carbon");
        CHECK(Has(rcmake, "set(SB_WINDOWS_EXE_NAME SpeedBreaker)\n"), "Most Wanted's exe stays SpeedBreaker");
        CHECK(Has(rcmake, "/PDBALTPATH:${SB_WINDOWS_EXE_NAME}.pdb"), "the debug record names the game's .pdb");
        const std::string rc = Read(w + "speedbreaker.rc");
        CHECK(Has(rc, "#ifdef SB_GAME_CARBON\n#define SB_RC_ICON \"speedbreaker-carbon.ico\"\n#define SB_RC_MANIFEST \"speedbreaker-carbon.manifest\""),
            "speedbreaker.rc: Carbon's icon and manifest");
        CHECK(Has(rc, "#define SB_RC_FILE \"SpeedBreaker-Carbon.exe\"") && Has(rc, "#define SB_RC_FILE \"SpeedBreaker.exe\""),
            "speedbreaker.rc: each game's file name");
        CHECK(Has(rc, "1 ICON SB_RC_ICON") && Has(rc, "1 24 SB_RC_MANIFEST"), "speedbreaker.rc uses them");
        const std::string ico = Read(w + "speedbreaker-carbon.ico");
        CHECK(ico.size() > 1000 && ico.compare(0, 4, std::string("\0\0\1\0", 4)) == 0, "speedbreaker-carbon.ico is an icon");
        CHECK(ico != Read(w + "speedbreaker.ico"), "Carbon's icon isn't Most Wanted's");
        CHECK(Has(Read(w + "speedbreaker-carbon.manifest"), "<description>SpeedBreaker: Carbon</description>"), "Carbon's manifest");
        for (const char* file : { "main.cpp", "gpu/renderer.cpp" })
        {
            const std::string text = Read(root + "/runtime/" + file);
            CHECK(Has(text, "\" SB_WINDOWS_LOGS \".\",\n") && !Has(text, "%LOCALAPPDATA%\\\\SpeedBreaker\\\\logs"), "%s's message box: the game's log folder", file);
        }
        CHECK(Has(Read(root + "/runtime/ui/installer_screen.cpp"), "std::string(SB_WINDOWS_EXE) : quoted(bundle / SB_WINDOWS_EXE)"),
            "installer_screen.cpp's Command Prompt hint names the game's exe");
        const std::string zip = Read(root + "/scripts/windows/make_zip.py");
        CHECK(Has(zip, "\"carbon\": {\"file\": \"SpeedBreaker-Carbon\", \"exe\": \"SpeedBreaker-Carbon\",\n               \"repository\": \"https://github.com/SpeedBreakerProject/speedbreaker-carbon\""),
            "make_zip.py: Carbon's download names");
        CHECK(Has(zip, "\"mw\": {\"file\": \"SpeedBreaker\", \"exe\": \"SpeedBreaker\",\n           \"repository\": \"https://github.com/SpeedBreakerProject/speedbreaker\""),
            "make_zip.py: Most Wanted's names as before");
    }

    // The iPhone and iPad app (scripts/ios): each game's Info.plist shows
    // SB_IOS_APP_NAME, the name the installer's and Settings' Files-app
    // hints use for its folder, and build.sh picks Carbon's plist and icon.
    void IosApp(const std::string& root)
    {
        const std::string ios = root + "/scripts/ios/";
#ifdef SB_GAME_CARBON
        const std::string plist = Read(ios + "SpeedBreaker/Info-carbon.plist");
        CHECK(Has(plist, "Need for Speed Carbon are trademarks"), "Info-carbon.plist's trademark line");
        CHECK(Has(Read(ios + "SpeedBreaker/Assets.xcassets/AppIcon-carbon.appiconset/Contents.json"), "\"AppIcon-carbon.png\""),
            "AppIcon-carbon.appiconset");
#else
        const std::string plist = Read(ios + "SpeedBreaker/Info.plist");
#endif
        CHECK(Has(plist, "<key>CFBundleDisplayName</key>\n\t<string>" SB_IOS_APP_NAME "</string>"),
            "the iOS Info.plist's CFBundleDisplayName is %s", SB_IOS_APP_NAME);
        const std::string build = Read(ios + "build.sh");
        CHECK(Has(build, "INFOPLIST_FILE=SpeedBreaker/Info-carbon.plist ASSETCATALOG_COMPILER_APPICON_NAME=AppIcon-carbon"),
            "build.sh: Carbon's plist and icon");
        CHECK(Has(build, "-DSB_GAME=\"$GAME\""), "build.sh configures SB_GAME");
        const std::string ipa = Read(ios + "make_ipa.sh");
        CHECK(Has(ipa, "VERSION_VAR=SB_VERSION_CARBON"), "make_ipa.sh: Carbon's version");
        const std::string installer = Read(root + "/runtime/ui/installer_screen.cpp");
        CHECK(Has(installer, "copy it into \" SB_IOS_APP_NAME \"'s folder in the Files app"), "installer_screen.cpp's Files folder");
        CHECK(!Has(installer, "SpeedBreaker's folder in the Files") && !Has(installer, "On My iPhone > SpeedBreaker)") &&
                !Has(installer, "in SpeedBreaker's folder..."), "installer_screen.cpp: no fixed Files folder name");
        CHECK(Has(Read(root + "/runtime/report/bug_report.cpp"), "\"the Files app, \" SB_IOS_APP_NAME \" folder: \""),
            "bug_report.cpp's Files folder");
        CHECK(Has(Read(root + "/runtime/ui/settings_menu.cpp"), "Files app (\" SB_IOS_APP_NAME \" folder)"),
            "settings_menu.cpp's Files folder");
    }

    // Each game's version (CMakeLists.txt) and its changelog's entry for it,
    // which the build turns into the "What's new" note.
    void Versions(const std::string& root)
    {
        const std::string cmake = Read(root + "/CMakeLists.txt");
        const std::string mw = CMakeVersion(cmake, "SB_VERSION_MW"), carbon = CMakeVersion(cmake, "SB_VERSION_CARBON");
        CHECK(!mw.empty() && !carbon.empty(), "set(SB_VERSION_MW/SB_VERSION_CARBON ...) in CMakeLists.txt");
        CHECK(Has(cmake, "project(SpeedBreaker VERSION ${SB_VERSION} LANGUAGES C CXX)"), "project() takes the game's version");
        CHECK(carbon == "0.1.0", "NFS: Carbon's first release: %s", carbon.c_str());
        // NFS: Carbon's repository: its own changelog is CHANGELOG.md, Most Wanted's CHANGELOG-mw.md.
        CHECK(Has(Read(root + "/CHANGELOG-mw.md"), "\n## v" + mw + "\n"), "CHANGELOG-mw.md has v%s", mw.c_str());
        const std::string carbonLog = Read(root + "/CHANGELOG.md");
        CHECK(Has(carbonLog, "\n## v" + carbon + "\n"), "CHANGELOG.md has v%s", carbon.c_str());
        CHECK(!Has(carbonLog, "## v0.1.9"), "CHANGELOG.md is Carbon's own");
        const std::string rcmake = Read(root + "/runtime/CMakeLists.txt");
        CHECK(Has(rcmake, "set(SB_CHANGELOG ${CMAKE_SOURCE_DIR}/CHANGELOG.md)"), "Carbon builds read CHANGELOG.md");
        CHECK(Has(rcmake, "set(SB_CHANGELOG ${CMAKE_SOURCE_DIR}/CHANGELOG-mw.md)"), "Most Wanted builds read CHANGELOG-mw.md");
        // Carbon's first entry (its What's new note) names every platform v0.1.0 ships for.
        const size_t entry = carbonLog.find("\n## v0.1.0\n");
        const size_t next = entry == std::string::npos ? entry : carbonLog.find("\n## v", entry + 1);
        const std::string first = entry == std::string::npos ? std::string() : Flat(carbonLog.substr(entry, next - entry));
        for (const char* platform : { "a Mac with Apple silicon,", "SteamOS (Steam Deck, Steam Machine and other x86-64 Linux PCs)",
                 "and the Steam Frame, as a big flat screen in the headset (not VR).",
                 "- **Windows (beta).** Windows 10 and 11 PCs", "as a beta, on its own pre-release download.",
                 "- **iPhone and iPad (experimental).** SpeedBreaker Carbon is its own app" })
            CHECK(Has(first, platform), "Carbon's v0.1.0 entry names %s", platform);
    }
}

int main(int argc, char** argv)
{
    Names();
    if (argc > 1)
    {
        Sources(argv[1]);
        IosApp(argv[1]);
        Versions(argv[1]);
        Windows(argv[1]);
    }
    else
        CHECK(false, "usage: game_identity_test <repository root>");
    if (g_failures)
    {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("game_identity_test (%s): all checks passed\n", SB_APP_NAME);
    return 0;
}
