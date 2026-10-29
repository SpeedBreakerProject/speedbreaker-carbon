// Tests for hid/wheel_map.h: a USB steering wheel as the Xbox 360 pad. Which joystick is a wheel,
// which of its axes is the wheel and which are its pedals, the steering curve (range, deadzone and
// response), a pedal from where it rests to as far as it is pressed, the buttons and the paddles,
// NFSMW_WHEEL_BUTTONS, and the force-feedback levels. Header only; from the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/wheel_map_test.cpp -o build/wheel_map_test && build/wheel_map_test
#include <hid/wheel_map.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <random>
#include <set>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

using namespace hid::wheelmap;

// Steer works on the turn as a fraction of full lock; these cases are
// written as raw axis readings, as the device gives them.
static float SteerRaw(int16_t raw, int range, int full, float deadzone, float response)
{
    return Steer(float(raw) / float(kAxisMax), range, full, deadzone, response);
}

static bool Near(float a, float b, float slack = 0.001f)
{
    return std::fabs(a - b) <= slack;
}

// A G29's axes as they rest, measured on the hardware: the wheel centred on
// axis 0, and its three pedals held at +32767 by their springs.
static constexpr int16_t kG29Rest[] = { 0, 32767, 32767, 32767 };

// Resolve takes a contiguous run of bools (std::vector<bool> is not one).
template <size_t N>
static std::array<bool, N> Held(std::initializer_list<int> which)
{
    std::array<bool, N> held{};
    for (int b : which)
        if (b >= 0 && size_t(b) < N)
            held[size_t(b)] = true;
    return held;
}


// Settings > Wheel > Calibrate, frame by frame: a wheel's axes, buttons and hat as SDL reads them, fed to the
// Calibration as wheel.cpp feeds it. A G29 by default: the wheel on axis 0, the clutch 1, the throttle 2, the brake 3,
// pedals resting at +32767; Cross (0) is A and Circle (2) is B.
struct Rig
{
    Calibration cal;
    std::vector<int16_t> axes{ 0, 32767, 32767, 32767 };
    std::vector<bool> seen = std::vector<bool>(4, true);
    std::array<bool, 25> buttons{};
    uint8_t hat = hat::Centred;
    int a = 0, b = 2;
    Calibration::Event last = Calibration::Event::None;

    void Start(int steerAxis = 0) { cal.Start(axes, seen, buttons, hat, steerAxis); }
    Calibration::Event Frame()
    {
        last = cal.Update(axes, seen, buttons, hat, a, b);
        return last;
    }
    // An axis moved to `to` over a few frames (each one a frame the calibration sees).
    Calibration::Event Move(int axis, int to, int frames = 6)
    {
        const int from = axes[size_t(axis)];
        Calibration::Event e = Calibration::Event::None;
        for (int f = 1; f <= frames && e == Calibration::Event::None; f++)
        {
            axes[size_t(axis)] = int16_t(from + (to - from) * f / frames);
            e = Frame();
        }
        return e;
    }
    Calibration::Event Down(int button) { buttons[size_t(button)] = true; return Frame(); }
    Calibration::Event Up(int button) { buttons[size_t(button)] = false; return Frame(); }
    Calibration::Event Tap(int button) { const auto e = Down(button); const auto f = Up(button); return e != Calibration::Event::None ? e : f; }
    Calibration::Event Hat(uint8_t bits) { hat = bits; const auto e = Frame(); hat = hat::Centred; Frame(); return e; }
};

int main()
{
    // The table: one fallback, last, and every other model a wheel we name,
    // with its own USB ids, an axis to steer with and buttons.
    CHECK(kModelCount >= 2, "%zu models", kModelCount);
    std::set<uint32_t> ids;
    for (size_t i = 0; i < kModelCount; i++)
    {
        const Model& m = kModels[i];
        const bool last = i == kModelCount - 1;
        CHECK((m.vendor == 0 && m.product == 0) == last, "model %zu: only the last is the fallback", i);
        CHECK(m.name != nullptr && m.axes.steer >= 0 && !m.buttons.empty(), "model %zu: named, steers, has buttons", i);
        // Where its pedals rest: an end of the axis, or 0 for not known.
        CHECK((m.pedalRest != m.pedalFull) == (m.vendor != 0),
            "model %zu: the wheels we know give both ends of their pedals, the fallback neither", i);
        if (m.pedalRest != m.pedalFull)
            CHECK((m.pedalRest > 0) != (m.pedalFull > 0), "model %zu: its pedals run from one end of the axis to the other", i);
        CHECK(m.fullTurnDegrees >= 0 && m.fullTurnDegrees <= 1080, "model %zu: %d degrees", i, m.fullTurnDegrees);
        if (!last)
            CHECK(ids.insert(uint32_t(m.vendor) << 16 | m.product).second, "model %zu: its own USB ids", i);
        // A pedal is never the wheel's own axis, and no two roles share one.
        const int axes[] = { m.axes.steer, m.axes.throttle, m.axes.brake, m.axes.clutch };
        for (size_t a = 0; a < 4; a++)
            for (size_t b = a + 1; b < 4; b++)
                CHECK(axes[a] < 0 || axes[a] != axes[b], "model %zu: axis %d is one role", i, axes[a]);
    }
    // The G29 is the wheel this was written for, and the one its numbers
    // were measured on: the wheel is axis 0, the pedals are throttle 2,
    // brake 3 and clutch 1 (not the order they stand in), they rest at
    // +32767, it turns 900 degrees, and its buttons are the Logitech set.
    for (uint16_t product : { 0xC24F, 0xC260 })
    {
        const Model& g29 = ModelFor(0x046D, product);
        CHECK(g29.vendor == 0x046D && g29.product == product, "the G29 (%04x) is known", product);
        CHECK(g29.axes.steer == 0 && g29.axes.throttle == 2 && g29.axes.brake == 3 && g29.axes.clutch == 1,
            "the G29's measured axes (%d, %d, %d, %d)", g29.axes.steer, g29.axes.throttle, g29.axes.brake, g29.axes.clutch);
        CHECK(g29.pedalRest == kAxisMax && g29.pedalFull == -32768, "the G29's pedals run +32767 to -32768");
        CHECK(g29.fullTurnDegrees == 900, "the G29 turns 900 degrees");
        CHECK(g29.buttons.data() == kLogitechButtons, "the G29 has the Logitech buttons");
    }
    // The models SDL 3.4.16's own Logitech driver reads (SDL_hidapi_lg4ff.c: HandleState sends the wheel as
    // axis 0 and each pedal byte, 255 resting, as byte * 257 - 32768 on the axis numbers below; OpenJoystick
    // gives the G29, G27 and G25 four axes and the Driving Force GT and Pro three). On a Mac this is the
    // layout these wheels arrive in, so a model has to match it: before any axis reports, its axes are used as
    // they are, and Detect only keeps them if they rest where pedals do.
    struct SdlLayout { uint16_t product; int axes, gas, brake, clutch; };
    for (const SdlLayout& d : { SdlLayout{ 0xC24F, 4, 2, 3, 1 }, SdlLayout{ 0xC29B, 4, 2, 3, 1 }, SdlLayout{ 0xC299, 4, 2, 3, 1 },
                                SdlLayout{ 0xC29A, 3, 1, 2, -1 }, SdlLayout{ 0xC298, 3, 1, 2, -1 } })
    {
        const Model& m = ModelFor(0x046D, d.product);
        CHECK(&m != &Fallback(), "%04x is a wheel we know", d.product);
        CHECK(m.axes.steer == 0 && m.axes.throttle == d.gas && m.axes.brake == d.brake && m.axes.clutch == d.clutch,
            "%04x: the model's axes (%d, %d, %d, %d) are SDL's (0, %d, %d, %d)", d.product, m.axes.steer, m.axes.throttle,
            m.axes.brake, m.axes.clutch, d.gas, d.brake, d.clutch);
        CHECK(m.axes.throttle < d.axes && m.axes.brake < d.axes && m.axes.clutch < d.axes, "%04x: every pedal is on an axis it has", d.product);
        std::vector<int16_t> rest(size_t(d.axes), int16_t(kAxisMax));
        rest[0] = 0;
        const Axes at = Detect(rest, m.axes);
        CHECK(at.throttle == d.gas && at.brake == d.brake && at.clutch == d.clutch, "%04x at rest: throttle %d, brake %d, clutch %d",
            d.product, at.throttle, at.brake, at.clutch);
        CHECK(m.pedalRest == kAxisMax && m.pedalFull == -32768, "%04x: pedals rest at +32767 and floor at -32768, as SDL sends them", d.product);
    }
    CHECK(&ModelFor(0x046D, 0x0001) == &Fallback(), "another Logitech device is not a wheel we know");
    CHECK(&ModelFor(0, 0) == &Fallback(), "no USB ids: the fallback");

    // Which joystick is a wheel. SDL's own word, our table, or the name;
    // but never something without an axis to steer with and a button.
    CHECK(LooksLikeWheel(true, 0x1234, 0x5678, "Something", 4, 12), "SDL says it is a wheel");
    CHECK(LooksLikeWheel(false, 0x046D, 0xC24F, "", 4, 25), "the G29 by its USB ids, unnamed");
    CHECK(LooksLikeWheel(false, 0, 0, "Thrustmaster T300RS", 4, 13), "an unknown wheel by its name");
    CHECK(LooksLikeWheel(false, 0, 0, "LOGITECH G923 Racing Wheel", 4, 13), "the name, whatever its case");
    CHECK(!LooksLikeWheel(false, 0x045E, 0x028E, "Xbox 360 Controller", 6, 11), "a controller is not a wheel");
    CHECK(!LooksLikeWheel(true, 0x046D, 0xC24F, "Logitech G29", 1, 1), "one axis: not a wheel, whatever it says");
    CHECK(!LooksLikeWheel(true, 0x046D, 0xC24F, "Logitech G29", 4, 0), "no buttons: nor is this");

    // Where the axes rest tells the wheel (centred) from the pedals (held at
    // one end): a G29 comes out as its model says.
    Axes axes = Detect(kG29Rest, ModelFor(0x046D, 0xC24F).axes);
    CHECK(axes.steer == 0 && axes.throttle == 2 && axes.brake == 3 && axes.clutch == 1, "the G29's axes: %d %d %d %d",
        axes.steer, axes.throttle, axes.brake, axes.clutch);

    // A wheel we don't know, with the same resting places, still steers and
    // brakes: the centred axis is the wheel, the rest are pedals in order.
    axes = Detect(kG29Rest, Fallback().axes);
    CHECK(axes.steer == 0 && axes.throttle == 1 && axes.brake == 2 && axes.clutch == 3, "an unknown wheel's axes");

    // A model that names the wrong axes loses to where they rest: here the
    // wheel is axis 2, which the model called a pedal. Its pedal layout
    // can't hold, so it is dropped whole and the axes left over go to the
    // pedals in the order the device reports them, rather than the throttle
    // keeping the model's axis while the brake takes what is left.
    constexpr int16_t kOddRest[] = { -32768, 32767, 0, 32767 };
    axes = Detect(kOddRest, { 0, 1, 2, 3 });
    CHECK(axes.steer == 2, "the centred axis is the wheel, not the one the model named: %d", axes.steer);
    CHECK(axes.throttle == 0 && axes.brake == 1 && axes.clutch == 3, "the rest in the device's order: %d, %d, %d",
        axes.throttle, axes.brake, axes.clutch);

    // Two pedals and no clutch: nothing is invented for a role the model
    // hasn't got, and a pedal is never the wheel's axis.
    constexpr int16_t kTwoPedals[] = { 0, 32767, 32767 };
    axes = Detect(kTwoPedals, { 0, 1, 2, -1 });
    CHECK(axes.steer == 0 && axes.throttle == 1 && axes.brake == 2 && axes.clutch == -1, "a wheel with two pedals");
    constexpr int16_t kOnePedal[] = { 0, 32767 };
    axes = Detect(kOnePedal, Fallback().axes);
    CHECK(axes.steer == 0 && axes.throttle == 1 && axes.brake == -1 && axes.clutch == -1, "a wheel with one pedal");

    // A wheel held off centre as the game starts is still the wheel (half
    // way is the line), and a pedal already pressed is still a pedal: here
    // axis 1, which on a G29 is the clutch, is held to the floor while the
    // other two rest, and every role still lands where the model says.
    constexpr int16_t kOffCentreRest[] = { 12000, -32768, 32767, 32767 };
    axes = Detect(kOffCentreRest, ModelFor(0x046D, 0xC24F).axes);
    CHECK(axes.steer == 0 && axes.throttle == 2 && axes.brake == 3 && axes.clutch == 1,
        "a wheel off centre, the clutch held down: %d, %d, %d, %d", axes.steer, axes.throttle, axes.brake, axes.clutch);

    // Preferred axes that name one axis for two pedals (wheel.cpp passes a stored calibration in as
    // `preferred`, and a calibration can go wrong) don't hold: no axis ever gets two roles.
    axes = Detect(kG29Rest, { 0, 2, 2, 1 });
    CHECK(axes.throttle != axes.brake && axes.throttle >= 0 && axes.brake >= 0,
        "throttle and brake both named axis 2: they end up apart (%d, %d)", axes.throttle, axes.brake);
    // ...whatever the device and whatever is preferred: every role on its own axis, the wheel never a pedal,
    // and a wheel found whenever an axis rests centred. A fixed seed, so a failure repeats.
    {
        std::mt19937 rng(29);
        auto pick = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
        int bad = 0;
        for (int trial = 0; trial < 50000 && bad < 3; trial++)
        {
            std::vector<int16_t> rest(size_t(pick(0, 8)));
            bool centred = false;
            for (int16_t& v : rest)
            {
                const int kind = pick(0, 3);
                v = int16_t(kind == 0 ? pick(-32768, 32767) : kind == 1 ? pick(-3000, 3000) : kind == 2 ? 32767 : -32768);
                centred = centred || RestsCentred(v);
            }
            const Axes pref = { pick(-1, 9), pick(-1, 9), pick(-1, 9), pick(-1, 9) };
            const Axes got = Detect(rest, pref);
            const int roles[] = { got.steer, got.throttle, got.brake, got.clutch };
            bool ok = (got.steer >= 0) == centred;
            for (int a = 0; a < 4; a++)
            {
                ok = ok && roles[a] >= -1 && roles[a] < int(rest.size());
                for (int b = a + 1; b < 4; b++)
                    ok = ok && (roles[a] < 0 || roles[a] != roles[b]);
            }
            if (!ok)
            {
                bad++;
                CHECK(false, "Detect, %zu axes, preferred %d %d %d %d: %d %d %d %d", rest.size(), pref.steer, pref.throttle,
                    pref.brake, pref.clutch, got.steer, got.throttle, got.brake, got.clutch);
            }
        }
    }

    // Steering. The whole turn, straight through: the ends and the middle.
    CHECK(Near(SteerRaw(0, 0, 900, 0.0f, 1.0f), 0.0f), "centred is straight ahead");
    CHECK(Near(SteerRaw(32767, 0, 900, 0.0f, 1.0f), 1.0f), "full right");
    CHECK(Near(SteerRaw(-32768, 0, 900, 0.0f, 1.0f), -1.0f), "full left (the extra count clamps)");
    CHECK(Near(SteerRaw(16384, 0, 900, 0.0f, 1.0f), 0.5f, 0.002f), "half way round");
    // A range smaller than the wheel's turn reaches full lock sooner: 450 of
    // a G29's 900 degrees means half a turn is already full lock.
    CHECK(Near(SteerRaw(16384, 450, 900, 0.0f, 1.0f), 1.0f), "450 of 900: half the turn is full lock");
    CHECK(Near(SteerRaw(8192, 450, 900, 0.0f, 1.0f), 0.5f, 0.002f), "...and a quarter of it is half lock");
    CHECK(Near(SteerRaw(-16384, 450, 900, 0.0f, 1.0f), -1.0f), "the same the other way");
    CHECK(Near(SteerRaw(32767, 450, 900, 0.0f, 1.0f), 1.0f), "past full lock is full lock");
    // A range at or above the wheel's turn, or a wheel whose turn we don't
    // know, uses the whole turn.
    CHECK(Near(SteerRaw(16384, 900, 900, 0.0f, 1.0f), 0.5f, 0.002f), "900 of 900 is the whole turn");
    CHECK(Near(SteerRaw(16384, 540, 0, 0.0f, 1.0f), 0.5f, 0.002f), "a wheel whose turn is unknown: the whole axis");
    // The deadzone: nothing inside it, and what is outside still reaches
    // full lock.
    CHECK(Near(SteerRaw(3000, 0, 900, 0.1f, 1.0f), 0.0f), "inside the deadzone: straight ahead");
    CHECK(Near(SteerRaw(-3000, 0, 900, 0.1f, 1.0f), 0.0f), "...either way");
    CHECK(SteerRaw(4000, 0, 900, 0.1f, 1.0f) > 0.0f, "just outside it: steering");
    CHECK(Near(SteerRaw(32767, 0, 900, 0.1f, 1.0f), 1.0f), "the deadzone doesn't cost full lock");
    // Response: above 1 is gentler near the centre, below 1 sharper, and
    // full lock stays full lock either way.
    CHECK(SteerRaw(16384, 0, 900, 0.0f, 2.0f) < SteerRaw(16384, 0, 900, 0.0f, 1.0f), "response 2: gentler near the centre");
    CHECK(SteerRaw(16384, 0, 900, 0.0f, 0.5f) > SteerRaw(16384, 0, 900, 0.0f, 1.0f), "response 0.5: sharper");
    for (float response : { 0.5f, 1.0f, 2.0f })
        CHECK(Near(SteerRaw(32767, 0, 900, 0.05f, response), 1.0f), "response %.1f still reaches full lock", double(response));
    // Nothing a hand-edited settings.toml holds can send it out of range.
    for (int raw : { -32768, -1, 0, 1, 32767 })
        for (float deadzone : { -1.0f, 0.0f, 0.5f, 2.0f })
            for (float response : { -1.0f, 0.0f, 1.0f, 3.0f })
            {
                const float v = SteerRaw(int16_t(raw), -5, 900, deadzone, response);
                CHECK(v >= -1.0f && v <= 1.0f && v == v, "raw %d, deadzone %.1f, response %.1f: %f", raw, double(deadzone),
                    double(response), double(v));
            }

    // A pedal is the span from where it rests to where it reads floored,
    // measured at both ends. A G29's rest at +32767 and run to -32768.
    Pedal pedal;
    pedal.Start(32767, -32768);
    CHECK(pedal.ready, "both ends given: it reads");
    CHECK(Near(pedal.Value(32767), 0.0f), "a pedal at rest is up");
    CHECK(Near(pedal.Value(0), 0.5f, 0.01f), "half way down");
    CHECK(Near(pedal.Value(-32768), 1.0f), "on the floor");
    // One wired the other way round works the same.
    pedal.Start(-32768, 32767);
    CHECK(Near(pedal.Value(-32768), 0.0f) && Near(pedal.Value(32767), 1.0f), "a pedal that runs the other way");
    // Only movement towards the pressed end counts: the other way is a
    // pedal on its way back up, which is not a pedal being pressed.
    pedal.Start(0, 32767);
    CHECK(Near(pedal.Value(0), 0.0f) && Near(pedal.Value(32767), 1.0f), "a pedal from the middle");
    CHECK(Near(pedal.Value(-32768), 0.0f), "...and the other way is still up, not full");
    // A pedal that stops short of the end of its axis still reads as
    // floored, because the span says how far it really goes. (A G29's
    // brake stops around -31740 when it is pressed hard.)
    pedal.Start(32767, -31740);
    CHECK(Near(pedal.Value(-31740), 1.0f), "a pedal that stops short still reaches the floor");
    CHECK(Near(pedal.Value(-32768), 1.0f), "...and past it is still the floor");
    CHECK(pedal.Value(0) > 0.4f && pedal.Value(0) < 0.6f, "half way is still half way");

    // Two pedals sharing one axis, one taking it positive and the other
    // negative: each reads its own half and ignores the other's. (Some
    // wheels report a combined pedal axis this way.)
    Pedal shareUp, shareDown;
    shareUp.Start(0, 32767);
    shareDown.Start(0, -32768);
    CHECK(Near(shareUp.Value(32767), 1.0f) && Near(shareDown.Value(32767), 0.0f), "the positive half is one pedal's");
    CHECK(Near(shareDown.Value(-32768), 1.0f) && Near(shareUp.Value(-32768), 0.0f), "the negative half is the other's");
    CHECK(Near(shareUp.Value(0), 0.0f) && Near(shareDown.Value(0), 0.0f), "at rest, neither is pressed");
    // At rest it is 0, not -0 (which the menu and the log printed as "-0%").
    for (const Pedal& p : { shareUp, shareDown, pedal })
        for (int raw : { -32768, -1, 0, 1, 32767 })
            CHECK(!std::signbit(p.Value(int16_t(raw))), "no -0 at %d", raw);

    // A pedal nobody has placed reads nothing, however far its axis is from
    // zero: SDL reports an axis only when it moves, so an untouched pedal
    // resting at +32767 would otherwise read as floored.
    const Pedal untouched;
    CHECK(!untouched.ready, "a fresh pedal knows nothing");
    for (int raw : { -32768, 0, 32767 })
        CHECK(untouched.Value(int16_t(raw)) == 0.0f, "an unplaced pedal reads nothing at %d", raw);
    Pedal degenerate;
    degenerate.Start(5000, 5000);
    CHECK(!degenerate.ready && degenerate.Value(0) == 0.0f, "two ends the same is no span at all");

    // An axis read the other way round (Invert Steering).
    CHECK(Flip(0) == 0, "centred stays centred");
    CHECK(Flip(32767) == -32767 && Flip(-32767) == 32767, "the ends swap");
    CHECK(Flip(-32768) == kAxisMax, "the extra count clamps rather than wrapping to itself");
    CHECK(Flip(Flip(12345)) == 12345, "twice is where it started");

    // Whatever it reads, the answer is a fraction.
    pedal.Start(32767, -32768);
    for (int raw : { -32768, -1000, 0, 1000, 32767 })
    {
        const float v = pedal.Value(int16_t(raw));
        CHECK(v >= 0.0f && v <= 1.0f, "pedal at %d: %f", raw, double(v));
    }

    // The wheel's own axis as two measured ends: the centre falls halfway
    // between them, each side is measured against its own end, and which
    // way round the wheel is needs nothing else said.
    Steering wheel;
    CHECK(!Steering{}.ready && Steering{}.left == -kAxisMax && Steering{}.right == kAxisMax, "a default Steering spans the axis, unmeasured");
    wheel.Start(-32768, 32767);
    CHECK(wheel.ready, "two ends given");
    CHECK(Near(wheel.Fraction(0), 0.0f, 0.01f), "the middle is straight ahead");
    CHECK(Near(wheel.Fraction(32767), 1.0f, 0.01f), "full right");
    CHECK(Near(wheel.Fraction(-32768), -1.0f, 0.01f), "full left");
    // A wheel whose axis runs the other way: the ends come out swapped, and
    // that alone turns it round.
    wheel.Start(32767, -32768);
    CHECK(Near(wheel.Fraction(32767), -1.0f, 0.01f), "the positive end is full left on a backwards wheel");
    CHECK(Near(wheel.Fraction(-32768), 1.0f, 0.01f), "...and the negative end is full right");
    // A wheel that reaches neither end of its axis, and sits off centre:
    // each side still reaches full lock.
    wheel.Start(-20000, 10000);
    CHECK(Near(wheel.Fraction(-20000), -1.0f, 0.01f) && Near(wheel.Fraction(10000), 1.0f, 0.01f), "a short, lopsided wheel");
    CHECK(Near(wheel.Fraction(-5000), 0.0f, 0.01f), "its own centre is straight ahead");
    CHECK(wheel.Fraction(32767) <= 1.0f && wheel.Fraction(-32768) >= -1.0f, "past its ends is still full lock");
    // Two ends the same is no span: straight ahead, whatever it reads.
    Steering flat;
    flat.Start(1000, 1000);
    CHECK(!flat.ready && Near(flat.Fraction(32767), 0.0f), "no span, no steering");

    // The buttons the player assigns sit on top of the model's, and a
    // button given to one control is taken off whatever else had it.
    {
        std::array<int, kControlCount> assigned{};
        assigned.fill(-1);
        assigned[size_t(Control::A)] = 7;  // A is the lever the model calls LB
        const std::vector<Binding> mine = WithAssigned(kLogitechButtons, assigned);
        CHECK(Resolve(Held<25>({ 7 }), hat::Centred, mine).buttons == button::A, "button 7 is now A");
        CHECK(Resolve(Held<25>({ 0 }), hat::Centred, mine).buttons == 0, "...and the model's A no longer answers");
        CHECK(Resolve(Held<25>({ 9 }), hat::Centred, mine).buttons == button::Start, "the rest of the model stands");
        // Nothing assigned leaves the model exactly as it was.
        assigned.fill(-1);
        CHECK(WithAssigned(kLogitechButtons, assigned).size() == std::size(kLogitechButtons), "no assignments, no change");
        // A paddle can be reassigned too, and keeps its own role.
        assigned[size_t(Control::ShiftUp)] = 2;
        const std::vector<Binding> shift = WithAssigned(kLogitechButtons, assigned);
        CHECK(Resolve(Held<25>({ 2 }), hat::Centred, shift).rightY > 0, "button 2 now shifts up");
        CHECK(Resolve(Held<25>({ 4 }), hat::Centred, shift).rightY == 0, "...and the model's paddle no longer does");
    }

    // ---- Calibrate (wheel_map.h's Calibration), as a player does it on a G29 ----
    {
        using Event = Calibration::Event;
        Rig r;
        r.Start();
        CHECK(r.cal.Running() && r.cal.Current() == Step::SteerLeft, "it starts at the wheel's left end");
        // Full left, then letting go (the wheel centres itself) before A: the end it reached is what is kept.
        r.Move(0, -32768);
        r.Move(0, 0);
        CHECK(r.cal.Watch().found && r.cal.Watch().full == -32768, "full left is the end reached: %d", int(r.cal.Watch().full));
        CHECK(r.Tap(0) == Event::Kept && r.cal.LastFound().axis == 0 && r.cal.LastFound().full == -32768 && r.cal.LastFound().rest == 0,
            "kept: axis 0, from 0 to -32768 (not the centre it was let back to)");
        // Full right begins with the wheel held at full left (A pressed while holding it): coming back to the centre
        // is not the right end, so A does nothing yet...
        r.axes[0] = -32768;
        r.Frame();
        r.Move(0, 0);
        CHECK(!r.cal.Watch().found && r.Tap(0) == Event::None && r.cal.Current() == Step::SteerRight,
            "back to the centre from full left is not full right");
        // ...until it turns right.
        r.Move(0, 32767);
        CHECK(r.Tap(0) == Event::Kept && r.cal.LastFound().full == 32767, "full right: +32767");
        r.Move(0, 0);
        // The throttle, held while A keeps it.
        r.Move(2, -32768);
        CHECK(r.Tap(0) == Event::Kept && r.cal.LastFound().axis == 2 && r.cal.LastFound().rest == 32767 &&
            r.cal.LastFound().full == -32768, "the throttle: axis 2, +32767 to -32768");
        // The brake: the throttle is let up during this step (a full sweep of its axis) and then the brake pressed.
        // The throttle's axis is measured from its own rest and only the other way, so its release counts for nothing.
        r.Move(2, 32767);
        CHECK(!r.cal.Watch().found, "letting the throttle up is not the brake");
        r.Move(3, -32768);
        CHECK(r.Tap(0) == Event::Kept && r.cal.LastFound().axis == 3, "the brake is axis 3, not the throttle's 2 (%d)",
            r.cal.LastFound().axis);
        // The clutch, the brake let up meanwhile; turning the wheel (its own axis) never counts for a pedal.
        r.Move(3, 32767);
        r.Move(0, -32768);
        r.Move(0, 0);
        CHECK(!r.cal.Watch().found, "the wheel's axis is never a pedal's");
        r.Move(1, -20000);
        CHECK(r.Tap(0) == Event::Kept && r.cal.LastFound().axis == 1 && r.cal.LastFound().full == -20000, "the clutch, half way: axis 1");
        r.Move(1, 32767);
        // A: Cross is still down from keeping the clutch... no, Tap let it go. Press Cross: it is the candidate; press it
        // again and it is kept. Cross can be A.
        CHECK(r.cal.Current() == Step::ButtonA, "the buttons follow");
        CHECK(r.Tap(0) == Event::None && r.cal.Watch().button == 0, "Cross pressed for A: the candidate");
        CHECK(r.Tap(0) == Event::Kept && r.cal.LastFound().button == 0, "Cross again: A is Cross");
        // B: Circle pressed and let go, then A. Circle doesn't stop a button step, and the press is kept through its release.
        CHECK(r.Tap(2) == Event::None && r.cal.Running() && r.cal.Watch().button == 2, "Circle for B: kept through its release, no stop");
        CHECK(r.Tap(0) == Event::Kept && r.cal.LastFound().button == 2, "A keeps Circle as B");
        // X: Square, another button replaces it, then that one again keeps it (on its press, so it is still held
        // as the Y step begins).
        r.Tap(1);
        r.Tap(10);
        CHECK(r.cal.Watch().button == 10, "another button replaces the candidate");
        CHECK(r.Down(10) == Event::Kept && r.cal.LastFound().button == 10 && r.cal.Current() == Step::ButtonY,
            "...and pressing it again keeps it");
        // A button held as a step begins is not a press until it is let go and pressed again.
        r.Frame();
        CHECK(!r.cal.Watch().found, "a button held as the step begins is no press");
        r.Up(10);
        CHECK(!r.cal.Watch().found, "nor is letting it go");
        r.Down(10);
        CHECK(r.cal.Watch().button == 10, "pressed again, it is");
        r.Up(10);
        // The D-pad: right skips (nothing kept), left goes back, down stops.
        CHECK(r.Hat(hat::Right) == Event::Skipped && r.cal.Current() == Step::ButtonLB, "D-pad right skips Y");
        CHECK(r.Hat(hat::Left) == Event::Back && r.cal.Current() == Step::ButtonY, "D-pad left goes back to Y");
        for (int i = 0; i < 20 && r.cal.Running(); i++)
            r.Hat(hat::Right);
        CHECK(!r.cal.Running(), "skipping past the last step ends it");
    }
    {
        using Event = Calibration::Event;
        // A wheel without a clutch (a Driving Force GT: three axes): the clutch step is skipped with the D-pad.
        Rig r;
        r.axes = { 0, 32767, 32767 };
        r.seen = std::vector<bool>(3, true);
        r.Start();
        r.Move(0, -32768); r.Tap(0); r.Move(0, 32767); r.Tap(0); r.Move(0, 0);
        r.Move(1, -32768); r.Tap(0); r.Move(1, 32767);
        r.Move(2, -32768); r.Tap(0); r.Move(2, 32767);
        CHECK(r.cal.Current() == Step::Clutch, "at the clutch");
        CHECK(r.Tap(0) == Event::None, "A with no clutch pressed: nothing to keep");
        CHECK(r.Hat(hat::Right) == Event::Skipped && r.cal.Current() == Step::ButtonA, "D-pad right skips the clutch");
        // B stops an axis step; D-pad down stops a button step.
        Rig s;
        s.Start();
        CHECK(s.Tap(2) == Event::Stopped && !s.cal.Running(), "B stops an axis step");
        Rig t;
        t.Start();
        for (int i = 0; i < 5; i++)
            t.Hat(hat::Right);
        CHECK(t.cal.Current() == Step::ButtonA && t.Hat(hat::Down) == Event::Stopped, "D-pad down stops a button step");
    }
    {
        using Event = Calibration::Event;
        // Two pedals on one axis, one each way (a combined pedal axis resting in the middle): each finds its own half,
        // and letting the first up is not the second.
        Rig r;
        r.axes = { 0, 0 };
        r.seen = std::vector<bool>(2, true);
        r.Start();
        r.Move(0, -32768); r.Tap(0); r.Move(0, 32767); r.Tap(0); r.Move(0, 0);
        r.Move(1, 32767);
        CHECK(r.Tap(0) == Event::Kept && r.cal.LastFound().axis == 1 && r.cal.LastFound().rest == 0 && r.cal.LastFound().full == 32767,
            "throttle: axis 1, 0 to +32767");
        r.Move(1, 0);
        CHECK(!r.cal.Watch().found, "the throttle back to the middle is not the brake");
        r.Move(1, -32768);
        CHECK(r.Tap(0) == Event::Kept && r.cal.LastFound().axis == 1 && r.cal.LastFound().rest == 0 &&
            r.cal.LastFound().full == -32768, "brake: the same axis, 0 to -32768");
    }
    {
        using Event = Calibration::Event;
        // An axis the device hasn't reported reads 0, which says nothing: measured from SDL's first report of it
        // (where it was before it moved), not from that 0.
        Rig r;
        r.axes = { 0, 0, 0, 0 };
        r.seen = { true, false, false, false };
        r.Start();
        r.Move(0, -32768); r.Tap(0); r.Move(0, 32767); r.Tap(0); r.Move(0, 0);
        r.cal.FirstValue(2, 32767);
        r.seen[2] = true;
        r.axes[2] = 32767;
        r.Move(2, -32768);
        CHECK(r.Tap(0) == Event::Kept && r.cal.LastFound().rest == 32767, "rest from the first report: %d", int(r.cal.LastFound().rest));
        // Keep from the keyboard with nothing found: nothing happens, the step carries on.
        CHECK(r.cal.Keep(r.axes) == Event::None && r.cal.Current() == Step::Brake, "A with nothing found: nothing");
        // Jitter doesn't count: an axis has to go a third of its travel.
        r.seen[3] = true;
        r.cal.FirstValue(3, 32767);
        r.axes[3] = 32767;
        r.Move(3, 32767 - kCalibrateMoved + 100);
        CHECK(!r.cal.Watch().found, "less than a third of the travel is not a press");
    }

    // Idle with a wheel (v0.1.7's refresh-rate notice waits for 3 s of idle controls): a wheel resting off its
    // centre with a little jitter is idle, as a stick in its deadzone is; turning it, even slowly, is not.
    {
        WheelStill still;
        int counted = 0;
        for (int frame = 0; frame < 600; frame++)  // 10 s at rest, 11% off centre (a G29 opened at 3623), +-0.1% jitter
            counted += still.Turned(0.11f + 0.001f * float((frame * 7) % 3 - 1));
        CHECK(counted == 0, "a resting wheel off its centre is idle (counted %d times)", counted);
        CHECK(still.Turned(0.2f), "a turn of 9%% of full lock is use");
        int slow = 0;
        for (int frame = 0; frame < 60; frame++)  // 0.5% of lock a frame: a slow turn
            slow += still.Turned(0.2f + 0.005f * float(frame));
        CHECK(slow >= 10, "a slow turn keeps counting as use (%d of 60 frames)", slow);
        WheelStill fresh;
        CHECK(!fresh.Turned(0.5f), "where it first is says nothing");
    }

    // Every control has a name and its own place in the order.
    for (size_t i = 0; i < kControlCount; i++)
        CHECK(kControls[i].name != nullptr && size_t(kControls[i].control) == i, "control %zu is itself", i);

    // The buttons. A G29's, by position: Cross is A, Circle is B, Square is
    // X, Triangle is Y; Share and Options are Back and Start.
    auto press = [](std::initializer_list<int> which) {
        return Resolve(Held<25>(which), hat::Centred, kLogitechButtons);
    };
    CHECK(press({ 0 }).buttons == button::A, "Cross is A");
    CHECK(press({ 1 }).buttons == button::X, "Square is X");
    CHECK(press({ 2 }).buttons == button::B, "Circle is B");
    CHECK(press({ 3 }).buttons == button::Y, "Triangle is Y");
    CHECK(press({ 8 }).buttons == button::Back, "Share is Back");
    CHECK(press({ 9 }).buttons == button::Start, "Options is Start");
    CHECK(press({ 8, 9 }).buttons == (button::Back | button::Start), "Share + Options is the settings chord");
    CHECK(press({ 6 }).buttons == button::RightShoulder && press({ 7 }).buttons == button::LeftShoulder, "the levers are the shoulders");
    CHECK(press({ 10 }).buttons == button::RightThumb && press({ 11 }).buttons == button::LeftThumb, "the stick clicks");
    // The paddles shift gears, which the game does with the right stick;
    // both at once is no change, as holding both keys of a pair is.
    CHECK(press({ 4 }).rightY > 0 && press({ 4 }).buttons == 0, "the right paddle pushes the stick up");
    CHECK(press({ 5 }).rightY < 0 && press({ 5 }).buttons == 0, "the left one pulls it down");
    CHECK(press({ 4, 5 }).rightY == 0, "both paddles: no gear change");
    CHECK(press({}).buttons == 0 && press({}).rightY == 0, "nothing held, nothing pressed");
    // A button the wheel reports but nothing is bound to does nothing, and a
    // binding for a button the wheel hasn't got is ignored rather than read
    // off the end.
    CHECK(press({ 20 }).buttons == 0, "an unbound button does nothing");
    CHECK(Resolve(Held<2>({ 0, 1 }), hat::Centred, kLogitechButtons).buttons == (button::A | button::X),
        "a wheel with two buttons: those two, and no reading past them");
    // The hat is the D-pad, corners included.
    const std::array<bool, 25> none{};
    CHECK(Resolve(none, hat::Up, kLogitechButtons).buttons == button::DpadUp, "the hat is the D-pad");
    CHECK(Resolve(none, uint8_t(hat::Left | hat::Down), kLogitechButtons).buttons == (button::DpadLeft | button::DpadDown),
        "its corners too");

    // NFSMW_WHEEL_BUTTONS: what it names changes, the rest of the model's
    // bindings stay, NONE takes one out, and nonsense is reported without
    // throwing the others away.
    std::string error = "x";
    std::vector<Binding> changed = ParseBindings("0=B,2=A", kLogitechButtons, error);
    CHECK(error.empty(), "a line that reads: \"%s\"", error.c_str());
    CHECK(Resolve(Held<25>({ 0 }), hat::Centred, changed).buttons == button::B, "button 0 is now B");
    CHECK(Resolve(Held<25>({ 9 }), hat::Centred, changed).buttons == button::Start, "...and the model's Start is untouched");
    changed = ParseBindings("9=NONE", kLogitechButtons, error);
    CHECK(error.empty() && Resolve(Held<25>({ 9 }), hat::Centred, changed).buttons == 0, "NONE takes a button out");
    changed = ParseBindings(" 4=START , nonsense , 5=WHAT ", kLogitechButtons, error);
    CHECK(error == "nonsense", "the first item it can't read: \"%s\"", error.c_str());
    CHECK(Resolve(Held<25>({ 4 }), hat::Centred, changed).buttons == button::Start, "the items that do read still count");
    CHECK(ParseBindings("", kLogitechButtons, error).size() == std::size(kLogitechButtons) && error.empty(), "an empty list changes nothing");
    // A button number past 255 is refused, however long (it used to overflow an int on the way: UBSan).
    for (const char* text : { "256=A", "99999999999=A", "4294967296=A", "000000000000000000004=A" })
    {
        changed = ParseBindings(text, kLogitechButtons, error);
        bool past = false;
        for (const Binding& b : changed)
            past = past || b.index < 0 || b.index > 255;
        const bool leadingZeros = text[0] == '0';
        CHECK(!past && (leadingZeros ? error.empty() : error == text), "\"%s\": refused (error \"%s\")", text, error.c_str());
    }
    CHECK(Resolve(Held<25>({ 4 }), hat::Centred, ParseBindings("000000000000000000004=A", kLogitechButtons, error)).buttons == button::A,
        "leading zeros are still button 4");

    // Every name is one of the pad's controls, and NameOf says which.
    for (const auto& [name, binding] : kNames)
        if (name != "NONE")
            CHECK(NameOf(binding) == name, "%s names itself", std::string(name).c_str());

    // Force feedback: the stronger motor the game asks for, at the share the
    // setting allows, and nothing at all at 0.
    CHECK(Near(RumbleStrength(65535, 0, 100), 1.0f), "a full rumble at 100%%");
    CHECK(Near(RumbleStrength(0, 65535, 100), 1.0f), "either motor");
    CHECK(Near(RumbleStrength(65535, 65535, 50), 0.5f), "half strength");
    CHECK(Near(RumbleStrength(65535, 0, 0), 0.0f), "0%%: nothing");
    CHECK(Near(RumbleStrength(0, 0, 100), 0.0f), "no request: nothing");
    for (int percent : { -10, 0, 50, 100, 1000 })
    {
        const float v = RumbleStrength(40000, 20000, percent);
        CHECK(v >= 0.0f && v <= 1.0f, "%d%%: %f", percent, double(v));
        CHECK(SpringLevel(percent) <= 0x7FFF, "%d%%: the spring's coefficient fits", percent);
    }
    CHECK(SpringLevel(0) == 0 && SpringSaturation(0) == 0, "no centring force is no spring");
    CHECK(SpringLevel(100) == 0x7FFF, "full centring force is the strongest spring");
    CHECK(SpringLevel(50) > 0 && SpringLevel(50) < SpringLevel(100), "and half is in between");

    // The game's own steering deadzone (round 2 of the G29 test: "I had to
    // turn the wheel more than I should have to before the car would
    // steer"). GameSteer is the game as measured in a race; a wheel's stick
    // is stepped past it, so the car follows the wheel from the first step.
    {
        // hid/input.cpp's ToShort: the fraction a stick reaches the game as.
        auto toShort = [](float v) { return int16_t(std::clamp(v * 32767.0f, -32768.0f, 32767.0f)); };
        CHECK(GameStickUnits(0) == 0 && GameStickUnits(32767) == 1000 && GameStickUnits(-32768) == -1000,
            "the guest's int: centre and both ends");
        CHECK(GameStickUnits(6553) == 200 && GameStickUnits(6586) == 201 && GameStickUnits(-6586) == -201,
            "and the steps either side of the deadzone's edge");
        // What the race measured (the stick swept in 1% steps, the car's steering read back).
        const float measured[][2] = { { 0.06f, 0.0f }, { 0.12f, 0.0f }, { 0.20f, 0.0f }, { 0.21f, 0.0125f }, { 0.25f, 0.0625f },
            { 0.30f, 0.125f }, { 0.40f, 0.25f }, { 0.60f, 0.5f }, { 0.80f, 0.75f }, { 1.0f, 1.0f } };
        for (const auto& [x, y] : measured)
        {
            CHECK(Near(GameSteer(toShort(x)), y, 0.0015f), "a pad's stick at %.2f steers %.4f (got %.4f)", double(x), double(y),
                double(GameSteer(toShort(x))));
            CHECK(Near(GameSteer(toShort(-x)), -y, 0.0015f), "and as far the other way at -%.2f", double(x));
        }
        // A wheel: the car steers as far as the wheel says, from the first step.
        float before = -2.0f;
        int worst = 0;
        for (int i = -10000; i <= 10000; i++)
        {
            const float w = float(i) / 10000.0f;
            const float y = GameSteer(toShort(StickPastGameDeadzone(w)));
            CHECK(std::abs(y - w) <= 0.5f / float(kGameSteerSteps) + 1e-4f, "wheel %+.4f: the car steers %+.5f", double(w), double(y));
            CHECK(y >= before, "wheel %+.4f: never less than a smaller turn's (%+.5f < %+.5f)", double(w), double(y), double(before));
            CHECK((y == 0.0f) == (std::abs(w) * float(kGameSteerSteps) < 0.5f), "wheel %+.4f: straight only within half a step",
                double(w));
            before = y;
            if (i > 0 && GameSteer(toShort(w)) == 0.0f)
                worst = i;
        }
        CHECK(worst >= 2000 && worst <= 2010, "without it, a wheel steered nothing up to 20%% of its turn (to %d/10000)", worst);
        CHECK(Near(GameSteer(toShort(StickPastGameDeadzone(0.15f))), 0.15f, 0.001f) && GameSteer(toShort(0.15f)) == 0.0f,
            "15%% of the turn: the car turns, where a pad's stick at 15%% doesn't");
        CHECK(StickPastGameDeadzone(0.0f) == 0.0f && toShort(StickPastGameDeadzone(1.0f)) == 32767 &&
                toShort(StickPastGameDeadzone(-1.0f)) == -32768,
            "straight is a centred stick; full lock is the stick's end");
    }

    // The centring force (round 4 of the G29 test: "a consistent click every
    // second" with the wheel held round a corner, none with it straight).
    // Builds up to 286 said the autocentre level again every second while no
    // knock played, and SDL says it by letting the spring go and taking it up
    // again. Now: once at the open, once at the device's first report, and
    // on a change of the setting. Never on a timer.
    {
        const float frame = 1000.0f / 60.0f;
        Centring c;
        CHECK(c.Due(50, false) && c.said == 50, "the open says it");
        CHECK(!c.Due(50, false), "and nothing more until something happens");
        // The wheel held at 30% of its turn for 10 s: its axis reports every
        // frame (deviceReported from the first), no knock plays, the setting
        // stays at 50%.
        int sends = 0;
        for (int f = 0; f < 600; f++)
            sends += c.Due(50, true);
        CHECK(sends == 1, "10 s held off centre: said once (SDL's driver cleared it at the first report), not %d times", sends);
        // The old rule over the same 10 s, for the record: once a second.
        int old = 0;
        float sinceSaid = 0.0f;
        for (int f = 0; f < 600; f++)
            if ((sinceSaid += frame) >= 1000.0f)
            {
                old++;
                sinceSaid = 0.0f;
            }
        CHECK(old >= 9, "the old rule said it %d times in those 10 s", old);
        CHECK(c.Due(70, true) && c.said == 70, "a change of the setting says it");
        CHECK(!c.Due(70, true), "once");
        CHECK(c.Due(-5, true) && c.said == 0 && !c.Due(0, true), "out of range is clamped, and said once");
        CHECK(c.sends == 4, "four times in all: open, first report, two changes (%d)", c.sends);
        Centring reopened;
        CHECK(reopened.Due(0, true) && !reopened.Due(0, true), "a wheel opened after its first report: said once");
    }

    // Knocks (rounds 2 and 3 of the G29 test: "a click whenever feedback
    // should occur", then the clicking back while driving). The game knocks
    // with one frame of full rumble; the wheel must get a thump that lasts,
    // never a restart, and never a jump in strength from one frame to the
    // next: a rise takes kKnockAttackMs, a fall kKnockReleaseMs.
    {
        using Action = Knock::Action;
        const float frame = 1000.0f / 60.0f;
        // Every frame of a run: the rules every knock keeps.
        struct Checked
        {
            Knock k;
            int starts = 0, stops = 0, changes = 0;
            float playedMs = 0.0f, peak = 0.0f;
            void Frame(float target, float ms, const char* what, int f)
            {
                const bool was = k.playing;
                const float before = k.level;
                const Action a = k.Step(target, ms);
                starts += a == Action::Start;
                stops += a == Action::Stop;
                changes += a == Action::Change;
                CHECK(a != Action::Start || !was, "%s, frame %d: never started again while it plays", what, f);
                CHECK(a != Action::Change || was, "%s, frame %d: a change only while it plays", what, f);
                CHECK(k.level <= before + ms / float(kKnockAttackMs) + 1e-6f, "%s, frame %d: rises no faster than the attack", what, f);
                CHECK(k.level >= before - ms / float(kKnockReleaseMs) - 1e-6f, "%s, frame %d: falls no faster than the release", what, f);
                CHECK(k.playing == (k.level > 0.0f), "%s, frame %d: plays exactly while there is something to play", what, f);
                if (k.playing)
                    playedMs += ms;
                peak = std::max(peak, k.level);
            }
        };

        // One frame of 32767/32767 at Force Feedback 100% (0.5), as the game
        // sends for a wall hit, then nothing.
        Checked one;
        for (int f = 0; f < 60; f++)
            one.Frame(f == 1 ? RumbleStrength(32767, 32767, 100) : 0.0f, frame, "one knock", f);
        CHECK(one.starts == 1 && one.stops == 1, "one frame of rumble is one knock (%d starts, %d stops)", one.starts, one.stops);
        CHECK(Near(one.peak, 0.5f), "it comes in whole within a frame (attack %d ms): %f", kKnockAttackMs, double(one.peak));
        CHECK(one.playedMs >= 0.5f * float(kKnockReleaseMs) - frame, "and it lasts: %.0f ms", double(one.playedMs));
        CHECK(one.changes >= 3, "let go of in steps (%d), not dropped", one.changes);

        // The game's own rumble from a drag race (the virtual G29's log, 86.08 to
        // 86.75 s; one value a frame): a ramp down, two one-frame knocks, and
        // the small values between.
        static constexpr uint16_t kRecorded[][2] = { { 3160, 0 }, { 2883, 0 }, { 2624, 0 }, { 32767, 32767 }, { 2159, 0 },
            { 1949, 0 }, { 1755, 0 }, { 1572, 0 }, { 1401, 0 }, { 1242, 0 }, { 774, 0 }, { 1011, 0 }, { 495, 0 }, { 756, 0 },
            { 361, 0 }, { 640, 0 }, { 295, 0 }, { 575, 0 }, { 166, 0 }, { 469, 0 }, { 81, 0 }, { 46, 0 }, { 0, 0 }, { 311, 0 },
            { 49151, 49151 }, { 223, 0 }, { 0, 0 }, { 136, 0 }, { 0, 0 }, { 133, 0 }, { 0, 0 }, { 113, 0 }, { 0, 0 }, { 96, 0 },
            { 0, 0 }, { 79, 0 }, { 0, 0 }, { 64, 0 }, { 0, 0 }, { 8, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
            { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
        for (int percent : { 100, 50 })
        {
            Checked r;
            for (size_t f = 0; f < std::size(kRecorded); f++)
                r.Frame(RumbleStrength(kRecorded[f][0], kRecorded[f][1], percent), frame, percent == 100 ? "drag 100%" : "drag 50%", int(f));
            CHECK(r.starts >= 1 && r.starts == r.stops && !r.k.playing, "%d%%: %d starts, %d stops", percent, r.starts, r.stops);
            CHECK(r.peak > 0.0f && r.peak <= 0.75f * float(percent) / 100.0f + 1e-6f, "%d%%: no stronger than the strongest knock (%f)",
                percent, double(r.peak));
        }

        // Round 3's session, as the game plays it around the settings menu:
        // scraping a wall (the game's rumble swinging 25-100% about three
        // times a second, a frame of jitter on top), the menu opening (the
        // game pauses: its rumble runs down to 0 and stays there), Force
        // Feedback stepped from 100% to 45% and back to 50% while the menu is
        // open, Vibration off and on, the menu closing, and the scrape again.
        // The wheel's knock comes back exactly as it was, at the new
        // strength: let go of when the game stops, started from nothing when
        // it starts again, never restarted or jumped in between.
        {
            Checked m;
            int percent = 100;
            bool vibration = true;
            int f = 0;
            auto scrape = [&](int frames) {
                for (int i = 0; i < frames; i++, f++)
                {
                    const double t = double(f) / 60.0;
                    double v = 0.62 + 0.38 * std::sin(t * 2.0 * 3.14159265 * 3.0) + ((f % 3) == 0 ? 0.04 : -0.02);
                    const uint16_t low = uint16_t(std::clamp(v, 0.0, 1.0) * 65535.0);
                    m.Frame(vibration ? RumbleStrength(low, 0, percent) : 0.0f, frame, "menu run", f);
                }
            };
            auto quiet = [&](int frames) {
                for (int i = 0; i < frames; i++, f++)
                    m.Frame(vibration ? RumbleStrength(0, 0, percent) : 0.0f, frame, "menu run", f);
            };
            scrape(180);
            const float before = m.peak;
            CHECK(m.starts == 1 && m.k.playing, "the scrape is one knock while it lasts (%d starts)", m.starts);
            quiet(30);  // the menu opens; the game's rumble runs down
            CHECK(!m.k.playing && m.stops == 1, "let go of while the game is paused");
            for (int step = 0; step < 11; step++)  // Force Feedback 100% -> 45% under the menu
            {
                percent -= 5;
                quiet(12);
            }
            percent = 50;
            vibration = false;
            quiet(20);
            vibration = true;
            quiet(20);
            CHECK(m.starts == 1 && !m.k.playing, "nothing plays under the menu, whatever the settings do");
            m.peak = 0.0f;
            scrape(180);  // the menu closes; scraping again
            CHECK(m.starts == 2 && m.k.playing, "the next scrape starts it again, from nothing");
            CHECK(Near(m.peak, 0.5f * before, 0.01f), "at half the strength now (%f, was %f)", double(m.peak), double(before));
            percent = 100;  // Force Feedback back up while it plays: a rise, at the attack
            scrape(60);
            CHECK(m.starts == 2, "a setting changed while it plays changes it in place");
        }

        // Nitrous (the G29 retest of build 287): the game asks for 3.5-4.4 s
        // of rumble at 75-100%, which shook the rim the whole time. Past
        // kKnockSustainMs it is a long rumble: eased, at the release, to
        // kKnockSustainScale of it, and held there. A crash's knock (a few
        // frames at full) is untouched, before and after.
        {
            Checked n;
            float lateMax = 0.0f;
            for (int f = 0; f < 240; f++)  // 4 s
            {
                const double t = double(f) / 60.0;
                const uint16_t v = uint16_t((0.875 + 0.125 * std::sin(t * 2.0 * 3.14159265 * 2.0)) * 65535.0);
                n.Frame(RumbleStrength(v, v, 100), frame, "nitrous", f);
                if (f * frame > float(kKnockSustainMs + kKnockReleaseMs) + frame)
                    lateMax = std::max(lateMax, n.k.level);
            }
            CHECK(n.starts == 1 && n.k.sustained, "nitrous is one long rumble (%d starts)", n.starts);
            CHECK(n.peak > 0.75f, "it starts as a knock (%f)", double(n.peak));
            CHECK(lateMax > 0.0f && lateMax <= kKnockSustainScale + 1e-6f, "then it is light: at most %.0f%% (%f)",
                double(kKnockSustainScale) * 100.0, double(lateMax));
            for (int f = 0; f < 30; f++)
                n.Frame(0.0f, frame, "nitrous ends", f);
            CHECK(!n.k.playing && !n.k.sustained && n.stops == 1, "let go of when it ends");

            // A crash after it: a few frames of full rumble, as strong as ever.
            Checked c;
            for (int f = 0; f < 60; f++)
                c.Frame(f >= 1 && f <= 6 ? 1.0f : 0.0f, frame, "crash", f);
            Checked c2 = n;
            c2.peak = 0.0f;
            for (int f = 0; f < 60; f++)
                c2.Frame(f >= 1 && f <= 6 ? 1.0f : 0.0f, frame, "crash after nitrous", f);
            CHECK(Near(c.peak, 1.0f) && !c.k.sustained, "a crash knock is full strength (%f)", double(c.peak));
            CHECK(Near(c2.peak, c.peak) && c2.starts == 2, "and the same after a long rumble (%f)", double(c2.peak));
            CHECK(c.playedMs >= 6.0f * frame + float(kKnockReleaseMs) - frame, "and it lasts as before: %.0f ms", double(c.playedMs));
        }

        // A steady request is followed within a step, and holds still.
        Knock s;
        CHECK(s.Step(0.0f, frame) == Action::None && !s.playing, "nothing asked: nothing");
        CHECK(s.Step(0.40f, frame) == Action::Start && Near(s.level, 0.40f), "a rumble starts it");
        CHECK(s.Step(0.41f, frame) == Action::Change && Near(s.sent, 0.41f), "a stronger one is followed");
        CHECK(s.Step(0.41f, frame) == Action::None, "the same: nothing to say");
        CHECK(s.Step(1.0f, frame) == Action::Change && s.level < 1.0f, "a knock on top rises at the attack, in place");
        CHECK(Knock::Magnitude(1.0f) == int16_t(std::lround(kKnockGain * 32767.0f)) && Knock::Magnitude(0.0f) == 0 &&
                Knock::Magnitude(2.0f) == Knock::Magnitude(1.0f),
            "a full rumble is kKnockGain of the sine's largest magnitude");
        CHECK(std::abs(Knock::Magnitude(RumbleStrength(65535, 65535, 50)) * 2 - Knock::Magnitude(1.0f)) <= 1,
            "Force Feedback 50%% is half the swing");
    }

    // Usable at compile time (constexpr), as hid/key_map.h's rules are.
    static_assert([] { Pedal p; p.Start(32767, -32768); return p.Value(32767) == 0.0f && p.Value(-32768) == 1.0f; }(),
        "constexpr pedal");
    static_assert(Resolve(std::array<bool, 4>{ true, false, false, false }, hat::Up, kLogitechButtons).buttons ==
        (button::A | button::DpadUp), "constexpr buttons");

    printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}
