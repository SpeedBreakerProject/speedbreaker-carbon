// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// ea_cpu_budget_test <ppc dir>: EA SND's mixer never expels voices for time
// on a host (apu/ea_cpu_budget.h; GitHub issue #35: one sound, usually the
// engine, much quieter until the next gear change).
//   1. EA's rule, as the model in the header computes it: a mix at its usual
//      cost on a MacBook is far under budget; one block held up 14 ms or more
//      (the mixer thread not run, as a busy or hot Mac does) puts the
//      three-block average over it, and EA would expel voices; a budget of
//      100% or more is never checked.
//   2. The policy: voices are expelled only with NFSMW_EA_CPU_BUDGET=1.
//   3. The generated code still matches what the header names (a different
//      XEX or recompiler config must fail here, not silently in a game): the
//      mixer thread runs the budget check each block; the check times the
//      mixer with mftb, keeps the three-block average, and calls the
//      over-budget step only when the budget is under 100% and the load is
//      over it; the over-budget step picks a voice and expels it with
//      (voice, 0) until it fits, returning to 0x826017C8 and 0x826017E4.
// Build by hand: c++ -std=c++20 -O2 -I runtime tests/ea_cpu_budget_test.cpp -o /tmp/ea_cpu_budget_test
#include <apu/ea_cpu_budget.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
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

    using namespace apu::ea_budget;

    constexpr double Ticks(double ms) { return ms * kTicksPerMs; }

    // Blocks of the mixer's own time (ms each) through EA's average, with
    // `voicesMs` of voice costs: the most a block was over, in ms of a block.
    double WorstOverMs(const std::vector<double>& blocksMs, double voicesMs)
    {
        Average average;
        double worst = -1e9;
        for (double ms : blocksMs)
            worst = std::max(worst, ToMs(Over(average.Add(Ticks(ms)), Ticks(voicesMs))));
        return worst;
    }

    void Rule()
    {
        // The budget: 90% of a 5.33 ms block, in EA's units (5.13 ms).
        Check(Budget(90.0) > 15.35e6 && Budget(90.0) < 15.37e6, "the budget is 15.36 M (EA's units)");
        Check(ToMs(Budget(90.0)) > 5.12 && ToMs(Budget(90.0)) < 5.14, "the budget is 5.13 ms of a block");
        // A MacBook's usual mix: ~0.02-0.1 ms of the mixer's own time and
        // ~0.5 ms of voices a block (the drag race): far under.
        Check(WorstOverMs(std::vector<double>(600, 0.1), 0.6) < -4.0, "a usual mix is far under budget");
        // One block held up 20 ms (measured: every SND9 voice expelled at
        // once, the mix 6-7 dB quieter until the next gear change).
        std::vector<double> held(300, 0.1);
        held[150] = 20.0;
        Check(WorstOverMs(held, 0.6) > 1.0, "one block held 20 ms puts EA over budget");
        // 14 ms is enough; 13 ms isn't (the three-block average).
        held[150] = 14.0;
        Check(WorstOverMs(held, 0.6) > 0.0, "one block held 14 ms puts EA over budget");
        held[150] = 13.0;
        Check(WorstOverMs(held, 0.6) < 0.0, "one block held 13 ms doesn't");
        // EA's average: three blocks, the oldest replaced.
        Average a;
        a.Add(3);
        a.Add(6);
        Check(a.Add(9) == 6.0 && a.Add(0) == 5.0 && a.Add(0) == 3.0 && a.Add(0) == 0.0, "the average is of the last three blocks");
        // A budget of 100% or more is never checked.
        Check(Over(Ticks(50), Ticks(50), 100.0) == 0.0, "a budget of 100% isn't checked");
    }

    void Policy()
    {
        static_assert(!Expels(nullptr));
        Check(!Expels(nullptr), "no NFSMW_EA_CPU_BUDGET: nothing expelled for time");
        Check(!Expels(""), "NFSMW_EA_CPU_BUDGET empty: nothing expelled");
        Check(!Expels("0"), "NFSMW_EA_CPU_BUDGET=0: nothing expelled");
        Check(!Expels("10"), "NFSMW_EA_CPU_BUDGET=10: nothing expelled");
        Check(Expels("1"), "NFSMW_EA_CPU_BUDGET=1: EA's rule");
    }

    // The generated functions: name -> body (the PPC_FUNC_IMPL block).
    std::map<std::string, std::string> LoadFunctions(const std::filesystem::path& ppc, const std::vector<std::string>& names)
    {
        std::map<std::string, std::string> found;
        for (const auto& entry : std::filesystem::directory_iterator(ppc))
        {
            const std::string file = entry.path().filename().string();
            if (file.rfind("ppc_recomp.", 0) != 0 || entry.path().extension() != ".cpp")
                continue;
            std::ifstream in(entry.path(), std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            const std::string text = ss.str();
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

    // `body` holds these lines in this order (each a substring of a line,
    // later ones after earlier ones; other lines may come between).
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

    std::string Name(uint32_t address)
    {
        char s[32];
        snprintf(s, sizeof(s), "sub_%08X", address);
        return s;
    }

    std::string Call(uint32_t address)
    {
        char s[32];
        snprintf(s, sizeof(s), "// bl 0x%08x", address);
        return s;
    }

    void GeneratedCode(const std::filesystem::path& ppc)
    {
        const std::vector<std::string> names = { Name(kMixerThread), Name(kBudgetCheck), Name(kExpelOverBudget), Name(kPickVoice),
            Name(kExpelVoice), Name(kTimebase) };
        auto f = LoadFunctions(ppc, names);
        const int before = s_failed;
        for (const std::string& n : names)
            Check(f.count(n) == 1, "ppc/ has " + n);
        if (s_failed != before)
            return;
        // The timebase: mftb and nothing else.
        Check(InOrder(f[Name(kTimebase)], { "// mftb r3", "// blr" }), "sub_825CFEB0 is mftb");
        // The mixer thread runs the check on mixer + 68 each block, after the
        // command buffer and before the voices.
        Check(InOrder(f[Name(kMixerThread)], { "// bl 0x825cf588", "// addi r3,r31,68", Call(kBudgetCheck), "// bl 0x825d05d0" }),
            "the mixer thread runs the budget check on mixer + 68 between the commands and the voices");
        // The check: the time since the last stamp (mftb), the three-block
        // average (x 1/3 at +12), then, only while the config's budget (+16)
        // is under 100, the load against it, and the over-budget step with
        // the difference.
        Check(InOrder(f[Name(kBudgetCheck)], { Call(kTimebase), "// lwz r11,4(r31)", "// lwz r10,8(r31)", "// fmuls f8,f9,f13",
                  "// stfs f8,0(r30)", Call(kTimebase), "// lfs f12,16(r11)", "// lfs f0,512(r10)", "// bge cr6,0x82601644",
                  "// lwz r11,100(r11)", "// lfs f3,48(r10)", "// fsubs f1,f0,f12", "// ble cr6,0x82601644", Call(kExpelOverBudget) }),
            "sub_82601528 averages mftb time and calls the over-budget step when over a budget under 100%");
        // The over-budget step: pick, take its cost off, expel (voice, 0),
        // until it fits; the return addresses the probes and the log rely on.
        Check(InOrder(f[Name(kExpelOverBudget)], { "// fmr f31,f1", Call(kPickVoice), "ctx.lr = 0x826017C8;", "// li r4,0",
                  "// lfs f13,48(r3)", Call(kExpelVoice), "ctx.lr = 0x826017E4;", "// bgt cr6,0x826017c0" }),
            "sub_82601788 picks a voice and expels it with (voice, 0) until it fits");
        // The pick: the lowest priority (+36) under 100 only.
        Check(InOrder(f[Name(kPickVoice)], { "// lwz r7,100(r11)", "// lfs f0,36(r9)", "// lfs f0,512(r11)", "// blt cr6,0x82601774" }),
            "sub_826016B8 picks the lowest-priority voice, only under 100");
        // Expelling: state 2, costs cleared, the expel command queued.
        Check(InOrder(f[Name(kExpelVoice)], { "// li r10,2", "// stb r10,64(r31)", "// stfs f0,48(r31)" }),
            "sub_825D04B8(voice, 0) marks the voice expelled");
    }

    // NFS: Carbon's EA SND (ea_cpu_budget.h, carbon): the same budget in its
    // newer build of the library, and the hook that keeps its voices.
    void CarbonGeneratedCode(const std::filesystem::path& ppc)
    {
        namespace c = apu::ea_budget::carbon;
        const std::vector<std::string> names = { Name(c::kMixerUpdate), Name(c::kBudgetCheck), Name(c::kVoiceCosts),
            Name(c::kExpelOverBudget), Name(c::kPickVoice), Name(c::kTimebase) };
        auto f = LoadFunctions(ppc, names);
        const int before = s_failed;
        for (const std::string& n : names)
            Check(f.count(n) == 1, "ppc-carbon/ has " + n);
        if (s_failed != before)
            return;
        Check(InOrder(f[Name(c::kTimebase)], { "// mftb r3", "// blr" }), "Carbon's sub_826AB240 is mftb");
        Check(InOrder(f[Name(c::kMixerUpdate)], { Call(c::kBudgetCheck) }), "Carbon's mixer update runs the budget check");
        // The check on the mixer at 0x82FF2B78 (lis -32001; +11128): the
        // time since the last stamp, the three-block average, then while the
        // config's share (+220) is under 100 the voices' costs, the budget
        // (+216 / (+212 x the factor) x the share) taken off, and the
        // over-budget step when over.
        Check(InOrder(f[Name(c::kBudgetCheck)], { Call(c::kTimebase), "// lis r11,-32001", "// addi r31,r11,11128",
                  "// fmuls f13,f12,f13", "// stfs f13,16(r31)", Call(c::kTimebase), "// lfs f12,220(r11)", "// fcmpu cr6,f12,f0",
                  "// bge cr6,0x826cdab8", "// lfs f11,216(r11)", "// lfs f10,212(r11)", Call(c::kVoiceCosts), "// lfs f0,-31608(r11)",
                  "// fmuls f0,f10,f0", "// fdivs f0,f11,f0", "// fnmsubs f1,f0,f12,f1", "// ble cr6,0x826cdab8",
                  Call(c::kExpelOverBudget) }),
            "Carbon's sub_826CD9D8 averages mftb time and calls the over-budget step when over a budget under 100%");
        // The voices' costs: the average (+16) scaled by the float at
        // 0x8204EC64 (lis -32251; -5020), then each voice's.
        Check(InOrder(f[Name(c::kVoiceCosts)], { "// lfs f13,16(r11)", "// lis r11,-32251", "// lfs f0,-5020(r11)",
                  "// fmuls f1,f13,f0", "// fmadds f1,f13,f0,f1" }),
            "Carbon's sub_826CDAE8 scales the average and adds the voices' costs");
        // The over-budget step: pick, take its cost off, mark it expelled
        // (+67 = 2), free it, until it fits.
        Check(InOrder(f[Name(c::kExpelOverBudget)], { "// fmr f30,f1", Call(c::kPickVoice), "// lfs f0,-5020(r30)",
                  "// fnmsubs f30,f13,f0,f30", "// li r11,2", "// stb r11,67(r3)", Call(c::kExpelVoice), "// bgt cr6,0x826cdc0c" }),
            "Carbon's sub_826CDBD0 picks a voice and expels it until it fits");
    }

    // The hook per game: Carbon's step in a Carbon build, Most Wanted's otherwise
    // (an SB_HOOK since v0.2.0: its PAL (Spanish) release reaches it too).
    void Hooks(const std::filesystem::path& source)
    {
        std::ifstream in(source, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        Check(InOrder(text, { "#ifdef SB_GAME_CARBON", "PPC_FUNC(sub_826CDBD0)", "if (s_eaExpels)", "__imp__sub_826CDBD0(ctx, base);",
                  "NoteSkipped(", "#else", "SB_HOOK(sub_82601788)", "if (s_eaExpels)", "SB_ORIGINAL(sub_82601788)(ctx, base);", "#endif" }),
            "ea_cpu_budget.cpp hooks Carbon's over-budget step in Carbon builds and Most Wanted's otherwise, each a no-op unless asked");
    }
}

int main(int argc, char** argv)
{
    Rule();
    Policy();
    if (argc > 3)
    {
        if (std::filesystem::is_directory(argv[2]))
            CarbonGeneratedCode(argv[2]);
        else
            printf("ea_cpu_budget_test: no %s: NFS: Carbon's generated code not checked\n", argv[2]);
        Hooks(argv[3]);
    }
    // "-": an NFS: Carbon build, whose checkout needn't have Most Wanted's ppc/.
    if (argc > 1 && std::string(argv[1]) == "-")
        printf("ea_cpu_budget_test: an NFS: Carbon build: Most Wanted's generated code not checked\n");
    else if (argc > 1)
        GeneratedCode(argv[1]);
    else
    {
        fprintf(stderr, "FAIL: usage: ea_cpu_budget_test <ppc dir | -> [<ppc-carbon dir> <ea_cpu_budget.cpp>]\n");
        s_failed++;
    }
    if (s_failed)
    {
        fprintf(stderr, "%d check(s) failed\n", s_failed);
        return 1;
    }
    printf("ea_cpu_budget_test: all checks passed\n");
    return 0;
}
