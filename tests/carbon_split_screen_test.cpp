// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// carbon_split_screen_test <ppc-carbon dir> <game/carbon/split_screen.cpp>:
// NFS: Carbon's split-screen test (game/carbon/split_screen.h; GitHub
// speedbreaker-carbon #5: "Player 2 press START" never took the second
// controller, because v0.1.1's hook said "never split screen").
//   1. The decode: 0 is one player, anything else two.
//   2. The generated code still matches what the header names (a different
//      XEX or recompiler config must fail here, not silently in a game):
//      the store's Set(3015) writes the byte at 0x82C64320 and Get(3015)
//      reads it; the front end sets it to 1 as two-player starts and to 0 at
//      its first state; a loading race turns it into its mode (1 = two
//      players) at 0x82C65418, which the game's IsSplitScreen reads; and no
//      other code loads or stores that byte.
//   3. The hook reads the header's byte through Decode, and is no longer
//      the v0.1.1 stub.
// Build by hand: c++ -std=c++20 -O2 -I runtime tests/carbon_split_screen_test.cpp -o /tmp/carbon_split_screen_test
#include <game/carbon/split_screen.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    int s_failed = 0;

    void Check(bool ok, const std::string& what)
    {
        if (!ok)
        {
            s_failed++;
            fprintf(stderr, "FAIL: %s\n", what.c_str());
        }
    }

    std::string Read(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    using namespace game::carbon_split;

    void DecodeRule()
    {
        Check(!Decode(0), "0: one player");
        Check(Decode(1), "1: split screen");
        Check(Decode(2), "any other nonzero byte reads as split screen, as Get(3015) does");
        // lis r,-32058 (0x82C60000) + 17184; the race mode: lis -32058 + 21528.
        Check(kSplitFlag == 0x82C60000u + 17184u, "the flag's address is lis -32058 + 17184");
        Check(kRaceMode == 0x82C60000u + 21528u, "the race mode's address is lis -32058 + 21528");
        Check(kSplitKey == 3015, "the store's key");
    }

    // The bodies of the named functions in the generated code.
    std::map<std::string, std::string> LoadFunctions(const std::filesystem::path& ppc, const std::vector<std::string>& names,
        std::vector<std::string>& files)
    {
        std::map<std::string, std::string> found;
        for (const auto& entry : std::filesystem::directory_iterator(ppc))
        {
            const std::string file = entry.path().filename().string();
            if (file.rfind("ppc_recomp.", 0) != 0 || entry.path().extension() != ".cpp")
                continue;
            const std::string text = Read(entry.path());
            files.push_back(text);
            for (const std::string& name : names)
            {
                const std::string head = "PPC_FUNC_IMPL(__imp__" + name + ") {";
                const size_t at = text.find(head);
                if (at == std::string::npos)
                    continue;
                const size_t end = text.find("\n}\n", at);
                found[name] = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            }
        }
        return found;
    }

    // `body` holds these lines in this order (each a substring of a line).
    bool InOrder(const std::string& body, const std::vector<std::string>& lines)
    {
        size_t at = 0;
        for (const std::string& l : lines)
        {
            at = body.find(l, at);
            if (at == std::string::npos)
                return false;
            at += l.size();
        }
        return true;
    }

    void GeneratedCode(const std::filesystem::path& ppc)
    {
        const std::vector<std::string> names = { "sub_821F24D0", "sub_821F4230", "sub_821F1F90", "sub_821F36F8", "sub_82671CE0",
            "sub_8266E518", "sub_82673150", "sub_82643540", "sub_824555E0", "sub_82529EE8", "sub_82408E50", "sub_82455D80" };
        std::vector<std::string> files;
        const auto f = LoadFunctions(ppc, names, files);
        for (const std::string& n : names)
            Check(f.count(n) == 1, n + " is in the generated code");
        if (s_failed)
            return;
        auto has = [&](const char* fn, const std::vector<std::string>& lines, const char* what) {
            Check(InOrder(f.at(fn), lines), std::string(fn) + ": " + what);
        };
        // The store: Set(3015, v) stores v > 0 in the byte; Get(3015) reads it.
        has("sub_821F24D0", { "// cmpwi cr6,r31,3015", "// lis r10,-32058", "// li r11,1", "// stb r11,17184(r10)" },
            "Set(3015) stores the flag");
        has("sub_821F4230", { "// lis r10,-32058", "// stb r11,17184(r10)" }, "the other setter stores the flag");
        has("sub_821F1F90", { "// addi r11,r28,-3007", "// lis r11,-32058", "// lbz r11,17184(r11)", "// stw r11,0(r30)" },
            "Get(3015) reads the flag");
        // The front end: 1 as two-player starts, 0 at the first state.
        has("sub_82671CE0", { "// li r5,1", "// li r4,3015", "// lwz r11,28(r11)" }, "state 13 sets split screen");
        has("sub_8266E518", { "// cmpwi cr6,r11,11", "// li r5,1", "// li r4,3015", "// lwz r11,28(r11)" }, "state 11 sets split screen");
        has("sub_82673150", { "// cmpwi cr6,r11,1", "// li r5,0", "// li r4,3015", "// lwz r11,28(r11)" }, "state 1 clears it");
        has("sub_82643540", { "// li r5,0", "// li r4,3015", "// lwz r11,28(r11)" }, "the other manager clears it");
        // A loading race: its mode from the flag (2 online, else the flag),
        // stored at 0x82C65418 (0x82C64818 + 3072), which IsSplitScreen reads.
        has("sub_824555E0", { "// li r4,3015", "// lwz r11,24(r11)", "// li r3,2", "// xori r3,r11,1" }, "the race's mode from the flag");
        has("sub_82529EE8", { "// lis r11,-32058", "// addi r28,r11,18456", "// stw r27,3072(r28)" }, "the race's mode stored");
        has("sub_82408E50", { "// lis r11,-32058", "// lwz r11,21528(r11)", "// addi r11,r11,-1", "// cntlzw r11,r11" },
            "IsSplitScreen: the mode is 1");
        has("sub_82455D80", { "// li r4,3015", "// lwz r11,24(r11)", "// addi r3,r11,3108" }, "InGameSplitScreen.bun on the flag");
        // No other code loads or stores the byte (any lbz/stb at +17184).
        const std::regex access(R"(// (lbz|stb|lhz|sth|lwz|stw) r\d+,17184\(r\d+\)\n)");
        int bytes = 0;
        for (const std::string& text : files)
            for (auto it = std::sregex_iterator(text.begin(), text.end(), access); it != std::sregex_iterator(); ++it)
            {
                const std::string op = (*it)[1];
                if (op == "lbz" || op == "stb")
                    bytes++;
            }
        Check(bytes == 4, "the flag's byte accesses are Get's two and Set's two (found " + std::to_string(bytes) + ")");
    }

    void Hook(const std::filesystem::path& hook)
    {
        const std::string text = std::regex_replace(Read(hook), std::regex("//[^\n]*"), "");
        Check(text.find("kSplitFlag") != std::string::npos, "the hook reads the header's flag");
        Check(text.find("Decode(") != std::string::npos, "the hook decodes it with the header's rule");
        Check(text.find("bool GameSaysSplit() { return false; }") == std::string::npos, "the hook is not v0.1.1's stub");
        Check(text.find("bool Enabled() { return false; }") == std::string::npos, "split screen is not disabled outright");
        Check(text.find("NFSMW_SPLIT_SCREEN") != std::string::npos, "NFSMW_SPLIT_SCREEN=0 still turns it off");
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: carbon_split_screen_test <ppc-carbon dir> <game/carbon/split_screen.cpp>\n");
        return 2;
    }
    DecodeRule();
    GeneratedCode(argv[1]);
    Hook(argv[2]);
    if (s_failed)
    {
        fprintf(stderr, "%d check(s) failed\n", s_failed);
        return 1;
    }
    printf("carbon_split_screen_test: all passed\n");
    return 0;
}
