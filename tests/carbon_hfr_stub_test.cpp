// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// NFS: Carbon's Smooth Motion stub (game/carbon/hfr.cpp) defines every
// function game/hfr.h declares in namespace hfr. A Carbon build compiles the
// stub instead of Most Wanted's game/hfr.cpp, so a declaration the shared
// code calls without a definition here breaks the link, and only where the
// call is compiled: v0.2.0's NoteLatePicture/NoteHoldChange are called by
// the presenter off Apple's systems, so the merge linked on the Mac and
// failed on SteamOS (2026-10-26).
//   carbon_hfr_stub_test <game/hfr.h> <game/carbon/hfr.cpp>
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace
{
    std::string Read(const char* path)
    {
        std::ifstream f(path);
        std::stringstream s;
        s << f.rdbuf();
        return s.str();
    }

    // The text of the first "namespace hfr { ... }" block (braces counted).
    std::string HfrBlock(const std::string& text)
    {
        const size_t at = text.find("namespace hfr\n{");
        if (at == std::string::npos)
            return {};
        size_t i = text.find('{', at);
        int depth = 0;
        for (size_t j = i; j < text.size(); j++)
        {
            if (text[j] == '{')
                depth++;
            else if (text[j] == '}' && --depth == 0)
                return text.substr(i + 1, j - i - 1);
        }
        return {};
    }

    // Comments out: a "Name(" in one must not count.
    std::string NoComments(const std::string& text)
    {
        return std::regex_replace(text, std::regex("//[^\n]*"), "");
    }
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: carbon_hfr_stub_test <game/hfr.h> <game/carbon/hfr.cpp>\n");
        return 2;
    }
    const std::string header = NoComments(HfrBlock(Read(argv[1])));
    const std::string stub = NoComments(HfrBlock(Read(argv[2])));
    if (header.empty() || stub.empty())
    {
        fprintf(stderr, "FAIL: no namespace hfr block in %s\n", header.empty() ? argv[1] : argv[2]);
        return 1;
    }
    std::set<std::string> declared, defined;
    const std::regex decl(R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*;)");
    for (std::sregex_iterator it(header.begin(), header.end(), decl), end; it != end; ++it)
        declared.insert((*it)[1]);
    const std::regex def(R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*\{)");
    for (std::sregex_iterator it(stub.begin(), stub.end(), def), end; it != end; ++it)
        defined.insert((*it)[1]);
    int failures = 0;
    for (const auto& name : declared)
        if (!defined.count(name))
        {
            fprintf(stderr, "FAIL: hfr::%s is declared in game/hfr.h but not defined in game/carbon/hfr.cpp\n", name.c_str());
            failures++;
        }
    if (declared.size() < 10)
    {
        fprintf(stderr, "FAIL: only %zu declarations found in game/hfr.h\n", declared.size());
        failures++;
    }
    printf("%zu hfr functions declared, %zu defined in the Carbon stub, %d missing\n", declared.size(), defined.size(), failures);
    return failures ? 1 : 0;
}
