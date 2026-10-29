// Tests for hid/player_slots.h (which controller is which player in split screen) and game/split_screen.h's decode
// of the game's own split-screen test; hid/input.cpp is read (argv[1]) to check it keeps users 1-3 behind the rule.
// Header only; from the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/player_slots_test.cpp -o build/player_slots_test &&
//   build/player_slots_test runtime/hid/input.cpp
#include <game/split_screen.h>
#include <hid/player_slots.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

using hid::PlayerSlots;
constexpr int64_t kMs = 1'000'000;
constexpr uint16_t kStart = 0x0010, kA = 0x1000;  // XInput's START and A

int main(int argc, char** argv)
{
    // The game's predicate: 2 players and the quick-race flag (Quick Race > Split Screen). Custom Race sets the flag
    // with 1 player; the flags' other bits (0x400: the Custom/Split menus) don't count.
    using namespace game::split_screen;
    CHECK(Decode(2, 4) && Decode(2, 0x404) && Decode(2, 0xFFFFFFFF), "split screen");
    CHECK(!Decode(1, 0x404) && !Decode(2, 0x400) && !Decode(2, 0) && !Decode(0, 4) && !Decode(3, 4), "not split screen");
    CHECK(!PlausibleObject(0) && !PlausibleObject(0x82A2C686) && !PlausibleObject(0xFFFFFFF0) && !PlausibleObject(0x7F000000),
        "no database: 0, misaligned, past guest memory");
    CHECK(PlausibleObject(0x40001000) && PlausibleObject(0xA0100000) && PlausibleObject(0x00020000) && PlausibleObject(0x82A30000),
        "heap, physical and image objects");

    // Single player: no other pad is a player, however it's used.
    {
        PlayerSlots s;
        CHECK(!s.NoteSplit(false, 0) && !s.Changed(), "starts in single player");
        CHECK(s.NoteInput(11, kStart, false, 1 * kMs) == 0, "a second pad pressing START in single player is no player");
        for (int u = 1; u < PlayerSlots::kUsers; u++)
            CHECK(!s.Connected(u), "user %d not connected", u);
    }

    // Split screen: Player 2 presses START, Player 3's pad joins too; a phantom never does.
    {
        PlayerSlots s;
        int64_t t = 0;
        CHECK(s.NoteSplit(true, t) && s.Changed(), "split screen on at once");
        CHECK(s.NoteSplit(true, t += 16 * kMs) && !s.Changed(), "and stays on");
        CHECK(s.NoteInput(21, 0, false, t) == 0 && s.NoteInput(22, 0, false, t) == 0, "pads at rest are nobody yet");
        for (int i = 0; i < 600; i++)
            s.NoteInput(23, 0, false, t + i * 16 * kMs);  // a phantom (Steam Input's, a dongle's): never any input
        CHECK(s.NoteInput(22, kStart, false, t += 16 * kMs) == 1, "the first pad used is user 1 (player 2)");
        CHECK(s.Connected(1) && s.PadOf(1) == 22 && s.UserOf(22) == 1, "user 1 is pad 22");
        // Arriving: idle for kArrivalNs (the game's PLUGGED flush, then a poll at rest), then the START that made it a
        // player held kReplayNs even if the tap was shorter (the join takes START's release), then the pad itself.
        CHECK(s.Arriving(1, t) && s.Arriving(1, t + PlayerSlots::kArrivalNs - 1), "arriving at first");
        CHECK(!s.Arriving(1, t + PlayerSlots::kArrivalNs), "then itself");
        CHECK(s.Shown(1, kStart, t) == 0 && s.Shown(1, kStart, t + PlayerSlots::kArrivalNs - 1) == 0, "nothing shown while arriving");
        CHECK(s.Shown(1, 0, t + PlayerSlots::kArrivalNs) == kStart, "a 50 ms tap, already released: START replayed");
        CHECK(s.Shown(1, kA, t + PlayerSlots::kArrivalNs + PlayerSlots::kReplayNs - 1) == (kStart | kA), "replayed with what's down now");
        CHECK(s.Shown(1, 0, t + PlayerSlots::kArrivalNs + PlayerSlots::kReplayNs) == 0, "then released: the press is whole");
        CHECK(s.Shown(1, kA, t + PlayerSlots::kArrivalNs + PlayerSlots::kReplayNs) == kA, "then the pad as it is");
        CHECK(s.Shown(2, kA, t) == kA && s.Shown(0, kA, t) == kA, "no other user is touched");
        CHECK(s.NoteInput(22, 0, false, t += 16 * kMs) == 1, "released: still user 1");
        CHECK(s.NoteInput(21, kStart, false, t += 16 * kMs) == 2, "another pad used: user 2");
        CHECK(s.UserOf(23) == 0 && !s.Connected(3), "the phantom is no player");
        CHECK(s.UserOf(99) == 0 && s.PadOf(0) == 0 && !s.Connected(0), "user 0 is player 1's, never here");

        // Player 2 unplugs mid-race: user 1 reads not connected, user 2 keeps its index (the game's ports don't move).
        CHECK(!s.Missing(1) && !s.Missing(2) && !s.Missing(0), "nobody missing while every player has a pad");
        CHECK(s.Remove(22) == 1, "removed: it was user 1");
        CHECK(!s.Connected(1) && s.Connected(2) && s.PadOf(2) == 21, "user 1 gone, user 2 stays");
        CHECK(s.Missing(1) && !s.Missing(2) && !s.Missing(3), "user 1 missing (the reconnect notice), only it");
        CHECK(s.Remove(22) == 0, "a second removal is nothing");
        // Plugged in again (a new SDL id): nobody until used, then its old user (the lowest free).
        CHECK(s.NoteInput(24, 0, false, t += 16 * kMs) == 0, "back, at rest: nobody yet");
        CHECK(s.Missing(1), "plugged in but not used yet: still missing");
        CHECK(s.NoteInput(24, kStart, false, t += 16 * kMs) == 1, "used: user 1 again");
        CHECK(!s.Missing(1), "a pad took user 1: not missing (the notice goes)");
        CHECK(s.Arriving(1, t), "arriving again");
        // A fourth pad takes the last user; a fifth gets none.
        CHECK(s.NoteInput(25, kStart, false, t) == 3 && s.NoteInput(26, kStart, false, t) == 0, "users 1-3 only");
        CHECK(s.Remove(26) == 0, "the fifth had none");

        // The game's predicate flickers between screens: nothing changes within kLeaveNs (of its last frame).
        CHECK(s.NoteSplit(true, t) && !s.Changed(), "still split screen this frame");
        CHECK(s.NoteSplit(false, t += 16 * kMs) && !s.Changed(), "a frame without it: still split screen");
        t -= 16 * kMs;
        CHECK(s.NoteSplit(false, t + PlayerSlots::kLeaveNs - 1) && s.Connected(1), "just under kLeaveNs: still");
        CHECK(s.NoteSplit(true, t + PlayerSlots::kLeaveNs) && !s.Changed() && s.Connected(1), "back on: nothing changed");
        t += PlayerSlots::kLeaveNs;
        // A player's pad gone just before split screen ends: missing until then, not after.
        CHECK(s.Remove(25) == 3 && s.Missing(3), "user 3's pad gone: missing");
        // Leaving: every user freed, and the pads forgotten.
        CHECK(s.NoteSplit(false, t += 16 * kMs) && !s.Changed(), "off, waiting");
        CHECK(!s.NoteSplit(false, t + PlayerSlots::kLeaveNs) && s.Changed(), "off after kLeaveNs");
        for (int u = 1; u < PlayerSlots::kUsers; u++)
            CHECK(!s.Connected(u) && !s.Missing(u), "user %d freed, not missing", u);
        CHECK(s.NoteInput(24, kStart, false, t + PlayerSlots::kLeaveNs) == 0, "single player again: no player");
        CHECK(s.Remove(24) == 0 && !s.Missing(1), "a pad going in single player is nobody missing");
        // A new split screen: a pad must be used again (it may be another's now).
        t += PlayerSlots::kLeaveNs;
        CHECK(s.NoteSplit(true, t) && s.Changed(), "split screen again");
        CHECK(s.NoteInput(21, 0, false, t) == 0, "pad 21 at rest: nobody");
        CHECK(s.NoteInput(24, kStart, false, t) == 1, "pad 24, used before split screen came back, is user 1 once used now");
    }

    // A trigger pressed past half makes a player too, with no button to replay.
    {
        PlayerSlots s;
        s.NoteSplit(true, 0);
        CHECK(s.NoteInput(41, 0, true, 0) == 1, "RT past half: user 1");
        CHECK(s.Shown(1, 0, PlayerSlots::kArrivalNs) == 0, "nothing replayed");
    }

    // Used before split screen began (pressing START at "Player 2 press START" is after it began, but a pad may have
    // been held): it becomes a player at split screen's first frame with input.
    {
        PlayerSlots s;
        CHECK(s.NoteInput(31, kStart, false, 0) == 0, "used in single player: no player");
        s.NoteSplit(true, 16 * kMs);
        CHECK(s.NoteInput(31, 0, false, 32 * kMs) == 1, "split screen: it was used, so it's user 1");
    }

    // input.cpp: users 1-3 answer from the per-user state only, rumble per user, the menu chord kept to player 1.
    if (argc > 1)
    {
        std::ifstream f(argv[1]);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string src = ss.str();
        CHECK(!src.empty(), "read %s", argv[1]);
        auto has = [&](const char* text) { return src.find(text) != std::string::npos; };
        CHECK(has("s_slots.NoteSplit(said, now)"), "the mode from the game's predicate");
        CHECK(has("game::split_screen::GameSaysSplit()"), "game/split_screen.h's predicate");
        CHECK(has("if (!split && s_others.empty())\n            return;"), "single player: nothing more");
        CHECK(has("if (s_slots.UserOf(ids[i]) != 0)\n                continue;"), "player 1 never takes another player's pad");
        CHECK(has("if (user && s_slots.Arriving(user, now))\n                p = Pad{};"), "arriving players read idle");
        CHECK(has("else if (user && !blocked)\n                p.buttons = s_slots.Shown(user, p.buttons, now);"), "then their press, not under a menu");
        CHECK(has("ou.rumbleLow = ByteSwap(vibration->wLeftMotorSpeed);"), "rumble per user");
        CHECK(has("return s_slots.UserOf(id) != 0;"), "IsOtherPlayersPad");
        CHECK(has("if (s_slots.Split())\n                OpenOtherPad(e.gdevice.which);"), "a pad plugged in during split screen opens");
        // Leaving split screen: the players were forgotten before their pads closed, so every user is reset explicitly
        // (v018 build 262 left XInput user 1 connected, frozen, after split screen).
        CHECK(has("CloseOtherPads(\"closed: split screen is over\");\n                {"), "users reset when split screen ends");
        CHECK(has("s_users[u].connected = false;"), "each user read not connected");
        CHECK(has("if (s_slots.Missing(u))\n                wanted |= 1u << u;"), "a missing player's notice");
        CHECK(has("ui::Toast(text, 24.0 * 3600.0);") && has("ui::WithdrawToast(text);"), "shown until withdrawn");
    }
    else
    {
        printf("  (no input.cpp given: its use not checked)\n");
    }

    if (g_failures)
    {
        printf("player_slots_test: %d FAILED\n", g_failures);
        return 1;
    }
    printf("player_slots_test: all passed\n");
    return 0;
}
