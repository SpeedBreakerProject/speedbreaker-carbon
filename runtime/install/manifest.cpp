// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See manifest.h.
#include "manifest.h"
#include <game/game_info.h>

#include <cassert>
#include <iterator>

// Each game's disc (scripts/gen_install_manifest.py writes both).
#ifdef SB_GAME_CARBON
#define SB_MANIFEST_INC "manifest_carbon.inc"
#else
#define SB_MANIFEST_INC "manifest.inc"
#endif

namespace install
{
    namespace
    {
        // One namespace per region: its files, then its MANIFEST_INFO.
#define MANIFEST_INFO(version, titleId, count, bytes)
#define MANIFEST_FILE(path, size, sha256) ManifestFile{ path, size, sha256 },
        // A Carbon build (SB_GAME_CARBON) has one: its USA Collector's
        // Edition (manifest_carbon.inc). The PAL (Spanish) disc is Most
        // Wanted's.
        namespace usa
        {
            constexpr ManifestFile s_files[] = {
#include SB_MANIFEST_INC
            };
        }
#ifndef SB_GAME_CARBON
        namespace pal_es
        {
            constexpr ManifestFile s_files[] = {
#include "manifest_pal_es.inc"
            };
        }
#endif
#undef MANIFEST_INFO
#undef MANIFEST_FILE

#define MANIFEST_INFO(version, titleId, count, bytes) \
        constexpr std::string_view s_version = version; \
        constexpr uint32_t s_titleId = titleId; \
        constexpr size_t s_count = count; \
        constexpr uint64_t s_bytes = bytes;
#define MANIFEST_FILE(path, size, sha256)
        namespace usa
        {
#include SB_MANIFEST_INC
        }
#ifndef SB_GAME_CARBON
        namespace pal_es
        {
#include "manifest_pal_es.inc"
        }
#endif
#undef MANIFEST_INFO
#undef MANIFEST_FILE

        template <size_t N>
        constexpr uint64_t SumSizes(const ManifestFile (&files)[N])
        {
            uint64_t sum = 0;
            for (const ManifestFile& f : files)
                sum += f.size;
            return sum;
        }

        template <size_t N>
        constexpr const ManifestFile* XexOf(const ManifestFile (&files)[N])
        {
            for (const ManifestFile& f : files)
                if (f.path == "default.xex")
                    return &f;
            return nullptr;
        }

        // A hand-edited or truncated .inc fails the build, not an install.
        static_assert(std::size(usa::s_files) == usa::s_count, SB_MANIFEST_INC ": file count does not match MANIFEST_INFO");
        static_assert(SumSizes(usa::s_files) == usa::s_bytes, SB_MANIFEST_INC ": sizes do not add up to MANIFEST_INFO's total");
        static_assert(XexOf(usa::s_files), SB_MANIFEST_INC ": no default.xex entry");
#ifndef SB_GAME_CARBON
        static_assert(std::size(pal_es::s_files) == pal_es::s_count, "manifest_pal_es.inc: file count does not match MANIFEST_INFO");
        static_assert(SumSizes(pal_es::s_files) == pal_es::s_bytes, "manifest_pal_es.inc: sizes do not add up to MANIFEST_INFO's total");
        static_assert(XexOf(pal_es::s_files), "manifest_pal_es.inc: no default.xex entry");
        // The regions are told apart by default.xex (hash, or size where no
        // hashing is done) and by the version an install's marker records.
        static_assert(XexOf(usa::s_files)->sha256 != XexOf(pal_es::s_files)->sha256, "two regions with one default.xex");
        static_assert(XexOf(usa::s_files)->size != XexOf(pal_es::s_files)->size, "two regions' default.xex of one size");
        static_assert(usa::s_version != pal_es::s_version, "two regions with one manifest version");
#endif

        constexpr Manifest s_usa{ usa::s_version, usa::s_titleId, usa::s_files, usa::s_bytes, "usa", "USA" };
#ifdef SB_GAME_CARBON
        constexpr const Manifest* s_manifests[] = { &s_usa };
#else
        constexpr Manifest s_palEs{ pal_es::s_version, pal_es::s_titleId, pal_es::s_files, pal_es::s_bytes, "pal-es",
            "Europe (PAL, Spanish)" };
        constexpr const Manifest* s_manifests[] = { &s_usa, &s_palEs };
#endif

#ifdef SB_GAME_CARBON
        // NFS: Carbon's standard USA edition: a different disc from the
        // Collector's Edition (its own default.xex, same title ID).
        constexpr KnownUnsupportedXex s_unsupported[] = {
            { "standard USA edition", 4861952,
                "947f95c7306eabf5bc72972938df57a922798bafbc02edf1a9107f0462c721e1",
                "This is the standard USA edition of " SB_GAME_NAME ". " SB_APP_NAME " supports the Collector's "
                "Edition for now (its case says Collector's Edition). Support for the standard edition is being "
                "considered: see \"Which copy do I need?\" in the README, https://github.com/" SB_GITHUB_REPO
                "#which-copy-do-i-need" },
        };
        static_assert(s_unsupported[0].size != XexOf(usa::s_files)->size, "the standard edition's default.xex is not the CE's");
        static_assert(s_unsupported[0].sha256 != XexOf(usa::s_files)->sha256, "the standard edition's default.xex is not the CE's");
        constexpr std::span<const KnownUnsupportedXex> s_unsupportedList = s_unsupported;
#else
        constexpr std::span<const KnownUnsupportedXex> s_unsupportedList = {};
#endif

        char Lower(char c)
        {
            return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
        }
    }

    bool PathEqualsIgnoreCase(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
            if (Lower(a[i]) != Lower(b[i]))
                return false;
        return true;
    }

    const ManifestFile& Manifest::Xex() const
    {
        const ManifestFile* xex = Find("default.xex");
        assert(xex && "a manifest must list default.xex");
        return *xex;
    }

    const ManifestFile* Manifest::Find(std::string_view path) const
    {
        for (const ManifestFile& f : files)
            if (PathEqualsIgnoreCase(f.path, path))
                return &f;
        return nullptr;
    }

    const Manifest& GameManifest()
    {
        return s_usa;
    }

    std::span<const Manifest* const> GameManifests()
    {
        return s_manifests;
    }

    bool IsGameManifest(const Manifest& manifest)
    {
        for (const Manifest* m : s_manifests)
            if (m == &manifest)
                return true;
        return false;
    }

    const Manifest* ManifestForXexHash(std::string_view sha256)
    {
        for (const Manifest* m : s_manifests)
            if (m->Xex().sha256 == sha256)
                return m;
        return nullptr;
    }

    const Manifest* ManifestForXexSize(uint64_t size)
    {
        for (const Manifest* m : s_manifests)
            if (m->Xex().size == size)
                return m;
        return nullptr;
    }

    const Manifest* ManifestForVersion(std::string_view version)
    {
        for (const Manifest* m : s_manifests)
            if (m->version == version)
                return m;
        return nullptr;
    }

    std::span<const KnownUnsupportedXex> KnownUnsupportedXexes()
    {
        return s_unsupportedList;
    }

    const KnownUnsupportedXex* KnownUnsupportedForXex(uint64_t size, std::string_view sha256)
    {
        for (const KnownUnsupportedXex& k : s_unsupportedList)
            if (k.size == size && k.sha256 == sha256)
                return &k;
        return nullptr;
    }
}
