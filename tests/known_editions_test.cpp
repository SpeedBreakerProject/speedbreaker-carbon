// Tests for the discs the installer knows by name but doesn't support
// (install/manifest.h KnownUnsupportedForXex): an NFS: Carbon build names the
// standard USA edition by its default.xex's size and SHA-256 (published disc
// metadata; no dump of it here, and no stand-in file can have that hash), a
// Most Wanted build knows none. Built twice by ctest: known_editions_test
// (Most Wanted) and known_editions_carbon_test (-DSB_GAME_CARBON). Given
// installer.cpp (ctest passes it), it also checks that CheckGameVersion, the
// one check behind the installer screen, --install and --verify, asks before
// its generic message and logs the line.
#include <game/game_info.h>
#include <install/manifest.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

using namespace install;

namespace
{
    constexpr uint64_t kStandardSize = 4861952;
    constexpr std::string_view kStandardSha = "947f95c7306eabf5bc72972938df57a922798bafbc02edf1a9107f0462c721e1";

    bool Has(std::string_view text, std::string_view what)
    {
        return text.find(what) != std::string_view::npos;
    }

    void Table()
    {
        const KnownUnsupportedXex* standard = KnownUnsupportedForXex(kStandardSize, kStandardSha);
#ifdef SB_GAME_CARBON
        CHECK(KnownUnsupportedXexes().size() == 1, "one known edition: %zu", KnownUnsupportedXexes().size());
        CHECK(standard != nullptr, "the standard USA edition by its default.xex");
        if (standard)
        {
            CHECK(standard->name == "standard USA edition", "%.*s", int(standard->name.size()), standard->name.data());
            const std::string_view m = standard->message;
            CHECK(m.starts_with("This is the standard USA edition of Need for Speed: Carbon. "), "%.*s", int(m.size()), m.data());
            CHECK(Has(m, "SpeedBreaker: Carbon supports the Collector's Edition for now (its case says Collector's Edition)."),
                "names the supported edition");
            CHECK(Has(m, "Which copy do I need?"), "names the README's section");
            CHECK(m.ends_with("https://github.com/SpeedBreakerProject/speedbreaker-carbon#which-copy-do-i-need"),
                "ends with the README's anchor: %.*s", int(m.size()), m.data());
            CHECK(!Has(m, "SHA-256"), "plain words, no hash");
        }
        // Both facts are needed: the size alone, or the hash alone, is not it.
        CHECK(!KnownUnsupportedForXex(kStandardSize + 4096, kStandardSha), "another size");
        CHECK(!KnownUnsupportedForXex(kStandardSize, "947f95c7306eabf5bc72972938df57a922798bafbc02edf1a9107f0462c721e0"),
            "another hash");
        CHECK(!KnownUnsupportedForXex(kStandardSize, "947F95C7306EABF5BC72972938DF57A922798BAFBC02EDF1A9107F0462C721E1"),
            "hashes are lower-case hex (ToHex)");
        // The supported disc is not in the list.
        const ManifestFile& ce = GameManifest().Xex();
        CHECK(ce.size == 4866048 && ce.sha256 == "922c7a1a2fc77405c43aac5159ad67fb0414c872084abc3335c68dbb4bec1815",
            "the Collector's Edition's default.xex");
        CHECK(!KnownUnsupportedForXex(ce.size, ce.sha256), "the Collector's Edition is supported, not known-unsupported");
        CHECK(ManifestForXexHash(kStandardSha) == nullptr, "no manifest takes the standard edition");
#else
        // Most Wanted: none; its messages stay as they were.
        CHECK(KnownUnsupportedXexes().empty(), "Most Wanted knows no unsupported edition: %zu", KnownUnsupportedXexes().size());
        CHECK(standard == nullptr, "Carbon's standard edition means nothing to Most Wanted");
#endif
    }

    // CheckGameVersion's body in installer.cpp.
    std::string CheckGameVersionBody(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        size_t at = text.find("    Result CheckGameVersion(");
        size_t end = at == std::string::npos ? at : text.find("\n    }\n", at);
        return end == std::string::npos ? std::string() : text.substr(at, end - at);
    }

    void Installer(const std::string& path)
    {
        const std::string body = CheckGameVersionBody(path);
        CHECK(!body.empty(), "CheckGameVersion in %s", path.c_str());
        const size_t ask = body.find("KnownUnsupportedForXex(id.file->size, found)");
        const size_t title = body.find("if (id.titleId != manifest.titleId)");
        const size_t generic = body.find("but not a version this build supports");
        CHECK(ask != std::string::npos, "CheckGameVersion asks KnownUnsupportedForXex");
        CHECK(ask < title && ask < generic, "before the title check and the generic message");
        CHECK(Has(body, "fprintf(stderr, \"[install] %.*s recognised (not supported yet)\\n\""), "the log line");
        CHECK(Has(body, "return Fail(Error::UnsupportedVersion, std::string(known->message), id.file->path);"),
            "its message, as UnsupportedVersion");
#ifdef SB_GAME_CARBON
        if (const KnownUnsupportedXex* k = KnownUnsupportedForXex(kStandardSize, kStandardSha))
        {
            char line[128];
            snprintf(line, sizeof line, "[install] %.*s recognised (not supported yet)", int(k->name.size()), k->name.data());
            CHECK(std::string_view(line) == "[install] standard USA edition recognised (not supported yet)", "%s", line);
        }
#endif
    }
}

int main(int argc, char** argv)
{
    Table();
    if (argc > 1)
        Installer(argv[1]);
    else
        CHECK(false, "usage: known_editions_test <runtime/install/installer.cpp>");
    if (g_failures)
    {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("known_editions_test (%s): all checks passed\n", SB_GAME_NAME);
    return 0;
}
