// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// A USB steering wheel as an Xbox 360 controller (Settings > Wheel): which
// of a joystick's axes is the wheel and which are its pedals, how far a
// turn and a pedal press reach, and which of its buttons are the pad's.
// Header only and SDL-free: hid/wheel.cpp samples the device and feeds the
// result to the pad, ui/settings_menu.cpp draws the page, and
// tests/wheel_map_test.cpp checks the rules.
//
// A wheel is not a gamepad: SDL has no gamepad mapping for one, so it
// arrives as a plain joystick whose axes and buttons mean nothing by
// themselves. Three things sort that out, in this order:
//
//   - The model table below, for the wheels we know (the Logitech G29 and
//     its family): which axis is the wheel, which pedal is which, how far
//     the wheel turns, and what its buttons are.
//   - Where the axes rest when the game opens the device, which tells a
//     wheel (centred) from a pedal (held at one end by its spring) whatever
//     the model says, so an unknown wheel still steers and brakes.
//   - The player, through Settings > Wheel > Calibrate, whose stored axis
//     numbers beat both (the pedals of a wheel we don't know are the one
//     thing the first two can get wrong: three springs look alike).
#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hid::wheelmap
{
    // XInput's button bits, as the game reads them (xbox.h's
    // XAMINPUT_GAMEPAD_*; wheel.cpp asserts they agree), as in key_map.h.
    namespace button
    {
        constexpr uint16_t DpadUp = 0x0001, DpadDown = 0x0002, DpadLeft = 0x0004, DpadRight = 0x0008;
        constexpr uint16_t Start = 0x0010, Back = 0x0020, LeftThumb = 0x0040, RightThumb = 0x0080;
        constexpr uint16_t LeftShoulder = 0x0100, RightShoulder = 0x0200;
        constexpr uint16_t A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000;
    }

    // A joystick hat's bits (SDL_HAT_*; wheel.cpp asserts they agree).
    namespace hat
    {
        constexpr uint8_t Centred = 0x00, Up = 0x01, Right = 0x02, Down = 0x04, Left = 0x08;
    }

    // The full deflection of a joystick axis, as SDL reports it. The
    // negative end is one more (-32768), which the readings clamp to this.
    constexpr int kAxisMax = 32767;

    // What one of a wheel's buttons does. The game shifts gears with the
    // right stick (README, Controls), so a wheel's paddles push it up and
    // down: ShiftUp and ShiftDown are not buttons the pad has.
    enum class Role : uint8_t { Button, ShiftUp, ShiftDown };

    struct Binding
    {
        int index = 0;             // the joystick button, as SDL numbers them
        uint16_t button = 0;       // Role::Button: which pad button it is
        Role role = Role::Button;
    };

    // Which axis is which. -1: the device hasn't got it (a wheel without a
    // clutch), or nothing has said which it is yet.
    struct Axes
    {
        int steer = -1, throttle = -1, brake = -1, clutch = -1;
    };

    // A wheel we know by its USB ids.
    struct Model
    {
        const char* name;
        uint16_t vendor = 0, product = 0;  // 0, 0: the fallback for every other wheel
        Axes axes;
        // Where its pedals rest and where they read floored. SDL can't be
        // asked: until the device's first report every axis reads 0, and it
        // sends no motion event for an axis until it moves (by more than
        // 1/80 of its travel), so wheel.cpp can't tell an untouched pedal
        // from one at 0. Equal values: unknown, and then a pedal reads
        // nothing until its axis speaks for itself.
        int16_t pedalRest = 0;
        int16_t pedalFull = 0;
        int fullTurnDegrees = 0;           // lock to lock; 0: unknown (Steering Range then has nothing to go by)
        std::span<const Binding> buttons;
    };

    // The Logitech wheels' buttons, in the PlayStation layout they report
    // over USB (the G29's own numbering, which the G920 and G923 share).
    // Matched to the pad by position, as a PlayStation controller's are
    // (README, Controls): Cross is A, Circle is B, Square is X, Triangle is
    // Y. The paddles shift gears; R2 and L2, the two levers behind them,
    // are the shoulder buttons.
    inline constexpr Binding kLogitechButtons[] = {
        { 0, button::A }, { 1, button::X }, { 2, button::B }, { 3, button::Y },
        { 4, 0, Role::ShiftUp },    // R1: the right paddle
        { 5, 0, Role::ShiftDown },  // L1: the left paddle
        { 6, button::RightShoulder }, { 7, button::LeftShoulder },
        { 8, button::Back },        // Share / Select
        { 9, button::Start },       // Options / Start
        { 10, button::RightThumb }, { 11, button::LeftThumb },
    };

    // Any other wheel: the order nearly every joystick reports its face
    // buttons in, so the menus have A, B and a way out of them. Settings >
    // Wheel's own rows (and NFSMW_WHEEL_BUTTONS) change it.
    inline constexpr Binding kGenericButtons[] = {
        { 0, button::A }, { 1, button::B }, { 2, button::X }, { 3, button::Y },
        { 4, button::LeftShoulder }, { 5, button::RightShoulder },
        { 6, button::Back }, { 7, button::Start },
        { 8, button::LeftThumb }, { 9, button::RightThumb },
    };

    // The wheels we know.
    //
    // The G29's numbers are measured, on a Logitech G29 (046d:c24f) over
    // USB on macOS: the wheel is axis 0 and turning left reads negative, as
    // the pad wants; the pedals are throttle 2, brake 3, clutch 1 - not the
    // order they sit in on the floor - and each rests at +32767 and runs to
    // -32768 as it goes down, which is the pedalRest and pedalFull pair
    // here. Its buttons are kLogitechButtons, confirmed
    // one at a time, and its hat is the D-pad.
    //
    // That is exactly what SDL 3.4's own Logitech driver
    // (src/joystick/hidapi/SDL_hidapi_lg4ff.c) makes of a G29's reports, and
    // the same driver gives the G27 and G25 those axes and the Driving Force
    // GT and Pro three (the gas on 1, the brake on 2): tests/wheel_map_test.cpp
    // holds that table. SDL's driver doesn't take the G920 or the G923, so
    // their entries are the G29's, guessed. Nothing here is trusted blindly
    // in any case: Detect() checks the axes against where they rest, and
    // Settings > Wheel > Calibrate overrules the lot.
    inline constexpr Model kModels[] = {
        { "Logitech G29 Driving Force", 0x046D, 0xC24F, { 0, 2, 3, 1 }, kAxisMax, -32768, 900, kLogitechButtons },
        { "Logitech G29 Driving Force", 0x046D, 0xC260, { 0, 2, 3, 1 }, kAxisMax, -32768, 900, kLogitechButtons },
        { "Logitech G920 Driving Force", 0x046D, 0xC262, { 0, 2, 3, 1 }, kAxisMax, -32768, 900, kLogitechButtons },
        { "Logitech G923 Driving Force", 0x046D, 0xC266, { 0, 2, 3, 1 }, kAxisMax, -32768, 900, kLogitechButtons },
        { "Logitech G923 Driving Force", 0x046D, 0xC26E, { 0, 2, 3, 1 }, kAxisMax, -32768, 900, kLogitechButtons },
        { "Logitech G27 Racing Wheel", 0x046D, 0xC29B, { 0, 2, 3, 1 }, kAxisMax, -32768, 900, kLogitechButtons },
        { "Logitech G25 Racing Wheel", 0x046D, 0xC299, { 0, 2, 3, 1 }, kAxisMax, -32768, 900, kLogitechButtons },
        // Three axes, not four: the wheel, then the gas and the brake (no
        // clutch), as SDL's driver reports them.
        { "Logitech Driving Force GT", 0x046D, 0xC29A, { 0, 1, 2, -1 }, kAxisMax, -32768, 900, kLogitechButtons },
        { "Logitech Driving Force Pro", 0x046D, 0xC298, { 0, 1, 2, -1 }, kAxisMax, -32768, 900, kLogitechButtons },
        { "Steering wheel", 0, 0, { 0, 1, 2, 3 }, 0, 0, 0, kGenericButtons },
    };
    constexpr size_t kModelCount = sizeof(kModels) / sizeof(kModels[0]);
    static_assert(kModels[kModelCount - 1].vendor == 0 && kModels[kModelCount - 1].product == 0,
        "the last model is the fallback for every wheel we don't know");

    inline const Model& Fallback()
    {
        return kModels[kModelCount - 1];
    }

    // The wheel with these USB ids, or the fallback.
    inline const Model& ModelFor(uint16_t vendor, uint16_t product)
    {
        for (const Model& m : kModels)
            if (m.vendor == vendor && m.product == product && m.vendor != 0)
                return m;
        return Fallback();
    }

    inline std::string Lowered(std::string_view text)
    {
        std::string out(text);
        for (char& c : out)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }

    // Is this joystick a wheel? SDL says so itself for the ones it knows
    // (SDL_JOYSTICK_TYPE_WHEEL), our own table for the rest, and failing
    // both the name, which is what a wheel SDL and we have never heard of
    // leaves to go on. `axes` and `buttons` are what the device reports: a
    // wheel has an axis to steer with and at least one pedal, so a name
    // alone never makes a two-axis gamepad into one.
    inline bool LooksLikeWheel(bool sdlSaysWheel, uint16_t vendor, uint16_t product, std::string_view name, int axes, int buttons)
    {
        if (axes < 2 || buttons < 1)
            return false;
        if (sdlSaysWheel)
            return true;
        if (&ModelFor(vendor, product) != &Fallback())
            return true;
        const std::string lowered = Lowered(name);
        for (std::string_view hint : { "wheel", "driving force", "racing", "g29", "g920", "g923", "g27", "g25",
                                       "thrustmaster", "fanatec", "logitech momo" })
            if (lowered.find(hint) != std::string::npos)
                return true;
        return false;
    }

    // Where an axis rests, as a share of full deflection: a wheel's own
    // axis rests centred, a pedal is held at one end by its spring. Half
    // way is nowhere near either, so it splits them with room to spare (a
    // wheel a little off centre at launch, a pedal that rests a touch short
    // of its end).
    constexpr float kRestsCentred = 0.5f;

    constexpr bool RestsCentred(int16_t rest)
    {
        constexpr int limit = int(kRestsCentred * kAxisMax);
        return rest >= -limit && rest <= limit;
    }

    // Which axis is which, for a device whose axes rest here. `preferred`
    // is the model's (or the player's, which wheel.cpp puts in first).
    //
    //   - The wheel's own axis is the preferred one if it rests centred,
    //     else the first axis that does. A wheel always has one and a pedal
    //     never does, so this holds for a wheel we have never heard of.
    //   - The pedals keep their preferred axes, but only if every one of
    //     them the device has is free and resting at an end, as a pedal
    //     under its spring does. If any doesn't check out, the model's
    //     pedal layout is wrong for this device and is dropped whole rather
    //     than half believed: the axes left over go to the throttle, the
    //     brake and the clutch in the order the device reports them, which
    //     is the order nearly every wheel has them in.
    //   - A clutch is looked for only where the model asked for one.
    //
    // So a device with one centred axis and its pedals at their ends steers
    // and brakes whatever we know about it, and a wheel whose pedals come
    // out in the wrong places is the one thing the player has to put right
    // (Settings > Wheel > Calibrate, whose axes arrive here as `preferred`,
    // and are not guesses: wheel.cpp keeps them whatever this says).
    inline Axes Detect(std::span<const int16_t> rest, const Axes& preferred)
    {
        const int count = int(rest.size());
        auto has = [&](int axis) { return axis >= 0 && axis < count; };
        Axes out;
        std::vector<bool> taken(size_t(std::max(0, count)), false);
        if (has(preferred.steer) && RestsCentred(rest[size_t(preferred.steer)]))
            out.steer = preferred.steer;
        else
            for (int a = 0; a < count; a++)
                if (RestsCentred(rest[size_t(a)]))
                {
                    out.steer = a;
                    break;
                }
        if (out.steer >= 0)
            taken[size_t(out.steer)] = true;
        int* const pedals[] = { &out.throttle, &out.brake, &out.clutch };
        const int wanted[] = { preferred.throttle, preferred.brake, preferred.clutch };
        // Does the model's pedal layout hold for this device? Its throttle
        // has to be there at all, and every pedal it names has to be free
        // and resting at an end.
        // One axis named for two pedals (a stored calibration gone wrong) can't
        // hold either: each pedal takes its axis as it is checked.
        bool layoutHolds = has(wanted[0]);
        std::vector<bool> checked = taken;
        for (size_t p = 0; p < 3 && layoutHolds; p++)
            if (has(wanted[p]))
            {
                if (checked[size_t(wanted[p])] || RestsCentred(rest[size_t(wanted[p])]))
                    layoutHolds = false;
                else
                    checked[size_t(wanted[p])] = true;
            }
        if (layoutHolds)
        {
            for (size_t p = 0; p < 3; p++)
                if (has(wanted[p]))
                {
                    *pedals[p] = wanted[p];
                    taken[size_t(wanted[p])] = true;
                }
            return out;
        }
        // It doesn't: the axes left over, in the order the device reports
        // them.
        for (int next = 0, p = 0; p < 3; p++)
        {
            if (p == 2 && wanted[2] < 0)
                break;  // this wheel has no clutch to find
            while (next < count && taken[size_t(next)])
                next++;
            if (next >= count)
                break;
            *pedals[p] = next;
            taken[size_t(next)] = true;
        }
        return out;
    }

    // A pedal, as the span from where it rests to where it reads fully
    // pressed. Both ends are measured - from the wheel's model, or from the
    // player pressing it in Settings > Wheel > Calibrate - because neither
    // can be assumed:
    //
    //   - which end a pedal rests at differs between wheels and between one
    //     wheel's modes (a G29 rests at +32767 and runs to -32768);
    //   - SDL reads every axis as 0 until the device first reports, and sends
    //     no event for one until it moves, so a pedal's 0 can mean nothing;
    //   - a pedal needn't reach the end of its axis, and the span says how
    //     far it really goes, so a floored pedal reads as floored;
    //   - and two pedals can share one axis, one taking it positive and the
    //     other negative. Each has its own span, so each reads only its own
    //     half and ignores the other's.
    //
    // Only movement towards the pressed end counts: the other way is a
    // pedal still coming up, which is to say not pressed.
    struct Pedal
    {
        int16_t idle = 0;  // where it rests
        int16_t full = 0;  // where it reads fully pressed
        bool ready = false;

        // Both ends. Equal ends (or the 0, 0 of a pedal nobody has placed)
        // leave it reading nothing, which is the safe way to be wrong.
        constexpr void Start(int16_t rest, int16_t pressed)
        {
            idle = rest;
            full = pressed;
            ready = rest != pressed;
        }

        constexpr float Value(int16_t raw) const
        {
            if (!ready)
                return 0.0f;
            const int32_t span = int32_t(full) - int32_t(idle);
            if (span == 0)
                return 0.0f;
            // max first, not clamp: a pedal at rest reads 0, not -0 ("-0%").
            return std::min(1.0f, std::max(0.0f, float(int32_t(raw) - int32_t(idle)) / float(span)));
        }
    };

    // The wheel's own axis, as the two places it reads at full lock. Like a
    // pedal, both ends are measured rather than assumed, which gives three
    // things at once: the centre (halfway between them), how far it turns
    // each way (a wheel needn't be symmetrical, or reach the ends of its
    // axis), and which way round it is - a wheel whose axis runs backwards
    // has its ends the other way about, and needs nothing else said.
    struct Steering
    {
        int16_t left = -kAxisMax, right = kAxisMax;
        bool ready = false;

        constexpr void Start(int16_t fullLeft, int16_t fullRight)
        {
            left = fullLeft;
            right = fullRight;
            ready = fullLeft != fullRight;
        }

        // -1 (full left) to 1, before Steering Range, the deadzone and the
        // response curve. Each side is measured against its own end, so an
        // off-centre or lopsided wheel still reaches full lock both ways.
        constexpr float Fraction(int16_t raw) const
        {
            const float centre = (float(left) + float(right)) * 0.5f;
            const float toRight = float(right) - centre;
            const float toLeft = float(left) - centre;
            if (toRight == 0.0f || toLeft == 0.0f)
                return 0.0f;
            const float v = float(raw) - centre;
            // Whichever side of centre the reading is on, against that side's end.
            const float t = (v * toRight >= 0.0f) ? v / toRight : -(v / toLeft);
            return std::clamp(t, -1.0f, 1.0f);
        }
    };

    // The controls Calibrate can learn a button for, in the order it asks
    // for them. The D-pad is left out: it comes from the hat, whose four
    // bits mean the same on every joystick SDL reports.
    enum class Control : uint8_t { A, B, X, Y, LB, RB, Back, Start, LS, RS, ShiftUp, ShiftDown, Count };
    constexpr size_t kControlCount = size_t(Control::Count);

    struct ControlInfo
    {
        Control control;
        uint16_t button;   // Role::Button: which pad button it is
        Role role;
        const char* name;  // what Calibrate calls it
    };

    inline constexpr ControlInfo kControls[kControlCount] = {
        { Control::A, button::A, Role::Button, "A" },
        { Control::B, button::B, Role::Button, "B" },
        { Control::X, button::X, Role::Button, "X" },
        { Control::Y, button::Y, Role::Button, "Y" },
        { Control::LB, button::LeftShoulder, Role::Button, "LB" },
        { Control::RB, button::RightShoulder, Role::Button, "RB" },
        { Control::Back, button::Back, Role::Button, "Back" },
        { Control::Start, button::Start, Role::Button, "Start" },
        { Control::LS, button::LeftThumb, Role::Button, "Left stick click" },
        { Control::RS, button::RightThumb, Role::Button, "Right stick click" },
        { Control::ShiftUp, 0, Role::ShiftUp, "Shift up" },
        { Control::ShiftDown, 0, Role::ShiftDown, "Shift down" },
    };

    consteval bool ControlsInOrder()
    {
        for (size_t i = 0; i < kControlCount; i++)
            if (size_t(kControls[i].control) != i)
                return false;
        return true;
    }
    static_assert(ControlsInOrder(), "one row per Control, in Control order");

    // The model's buttons with the player's own on top: `assigned[i]` is the
    // joystick button Calibrate recorded for Control(i), or -1 for none of
    // its doing. A button the player gives to one control is taken off
    // whatever else had it, so no press can mean two things.
    inline std::vector<Binding> WithAssigned(std::span<const Binding> from, std::span<const int> assigned)
    {
        std::vector<Binding> out(from.begin(), from.end());
        for (size_t i = 0; i < assigned.size() && i < kControlCount; i++)
        {
            const int index = assigned[i];
            if (index < 0)
                continue;
            const ControlInfo& c = kControls[i];
            std::erase_if(out, [&](const Binding& b) {
                return b.index == index || (b.role == c.role && (c.role != Role::Button || b.button == c.button));
            });
            out.push_back({ index, c.button, c.role });
        }
        std::stable_sort(out.begin(), out.end(), [](const Binding& a, const Binding& b) { return a.index < b.index; });
        return out;
    }

    // ---- Settings > Wheel > Calibrate --------------------------------------
    //
    // One step at a time: the wheel's two ends, each pedal, then a button for
    // every pad control. hid/wheel.cpp feeds the device in every frame
    // (Calibration::Update) and stores each step as it is kept; the menu draws
    // it and passes on the keyboard's and a controller's keys. Kept here,
    // SDL-free, so tests/wheel_map_test.cpp can drive it frame by frame.
    //
    //   - An axis step watches every axis and takes the one that has gone
    //     furthest from where it started the step - or, on an axis an earlier
    //     pedal of this run took, from that pedal's resting place and only the
    //     other way. So letting the throttle up while pressing the brake (a full
    //     sweep of the throttle's axis) counts for nothing in the brake's step,
    //     while two pedals sharing one axis, one each way, still each find their
    //     half. The wheel's own axis is never a pedal's, and the second end of
    //     the wheel is looked for on the axis the first was found on.
    //   - What an axis step keeps is the end the axis reached, not where it
    //     happens to be when A is pressed: let the wheel go and it is still the
    //     lock it reached.
    //   - A button step takes a fresh press (a button going down; one held as
    //     the step began counts once it has been let go and pressed again) and
    //     keeps it through its release. Pressing A then, or the same button
    //     again, keeps it; any other button replaces it. So Cross can be A (press
    //     it, then press it again) and Circle can be B.
    //   - The wheel's D-pad (its hat, never one of its buttons) moves Calibrate
    //     along: right skips a step (a wheel without a clutch), left goes back
    //     one, down stops. In an axis step the wheel's A keeps and its B stops.
    enum class Step : uint8_t
    {
        SteerLeft, SteerRight, Throttle, Brake, Clutch,
        ButtonA, ButtonB, ButtonX, ButtonY, ButtonLB, ButtonRB,
        ButtonBack, ButtonStart, ButtonLS, ButtonRS, ShiftUp, ShiftDown,
        Count
    };
    constexpr size_t kStepCount = size_t(Step::Count);
    static_assert(size_t(Step::Count) - size_t(Step::ButtonA) == kControlCount, "a button step per Control, in Control order");
    constexpr bool IsSteerStep(Step s) { return s == Step::SteerLeft || s == Step::SteerRight; }
    constexpr bool IsPedalStep(Step s) { return s >= Step::Throttle && s <= Step::Clutch; }
    constexpr bool IsButtonStep(Step s) { return s >= Step::ButtonA && s < Step::Count; }
    // A pedal step's slot (0 the throttle, 1 the brake, 2 the clutch) and back.
    constexpr int PedalSlot(Step s) { return int(s) - int(Step::Throttle); }
    constexpr Step PedalStep(int slot) { return Step(int(Step::Throttle) + slot); }
    constexpr Control ControlOf(Step s) { return Control(size_t(s) - size_t(Step::ButtonA)); }
    static_assert(PedalStep(0) == Step::Throttle && PedalStep(2) == Step::Clutch && PedalSlot(Step::Brake) == 1, "pedal slots");

    // How far an axis must move in a step to be the one meant: a third of its
    // travel, which no amount of jitter reaches and a deliberate press passes.
    constexpr int kCalibrateMoved = kAxisMax / 3;
    // The joystick buttons a step watches (the wheels we know have 25).
    constexpr size_t kCalibrateButtons = 64;

    class Calibration
    {
    public:
        enum class Event : uint8_t { None, Kept, Skipped, Back, Stopped };

        // What the step under way has found (Watch), and what a kept step
        // stored (LastFound): an axis with where it rested and the end it
        // reached, or a button.
        struct Found
        {
            bool found = false;
            int axis = -1;
            int16_t rest = 0, full = 0;
            float moved = 0.0f;  // how far it went, as a share of its travel
            int button = -1;
        };

        // `seen`: which axes the device has reported (one that hasn't reads 0,
        // which says nothing about where it is). `steerAxis`: the wheel's own
        // axis as the device is read now, never a pedal's; this run's steering
        // steps replace it.
        void Start(std::span<const int16_t> axes, const std::vector<bool>& seen, std::span<const bool> buttons, uint8_t hat,
            int steerAxis)
        {
            running_ = true;
            step_ = last_ = Step::SteerLeft;
            steerAxis_ = steerAxis;
            results_ = {};
            prev_ = {};
            for (size_t b = 0; b < prev_.size() && b < buttons.size(); b++)
                prev_[b] = buttons[b];
            prevHat_ = hat;
            seen_ = seen;
            BeginStep(axes);
        }

        bool Running() const { return running_; }
        Step Current() const { return step_; }
        // After Kept, Skipped, Back or Stopped: the step it happened to; after
        // Kept, what that step stored.
        Step LastStep() const { return last_; }
        Found LastFound() const { return results_[size_t(last_)]; }

        // SDL's first report of an axis carries where it was before it moved
        // (wheel.cpp passes it on): an axis that hadn't reported when the step
        // began is measured from there.
        void FirstValue(int axis, int16_t value)
        {
            if (!running_ || axis < 0 || size_t(axis) >= fromFirst_.size() || !fromFirst_[size_t(axis)])
                return;
            fromFirst_[size_t(axis)] = false;
            ref_[size_t(axis)] = value;
            far_[size_t(axis)] = value;
            dist_[size_t(axis)] = -1;
        }

        // One frame of the device. `aButton`, `bButton`: the joystick buttons
        // that are the pad's A and B now (-1: none).
        Event Update(std::span<const int16_t> axes, const std::vector<bool>& seen, std::span<const bool> buttons, uint8_t hat,
            int aButton, int bButton)
        {
            if (!running_)
                return Event::None;
            seen_ = seen;
            std::array<bool, kCalibrateButtons> pressed{};
            for (size_t b = 0; b < pressed.size(); b++)
            {
                const bool down = b < buttons.size() && buttons[b];
                pressed[b] = down && !prev_[b];
                prev_[b] = down;
            }
            const uint8_t newHat = uint8_t(hat & ~prevHat_);
            prevHat_ = hat;
            if (newHat & hat::Down)
                return Stop();
            if (newHat & hat::Right)
                return Skip(axes);
            if (newHat & hat::Left)
                return Back(axes);
            if (IsButtonStep(step_))
            {
                for (size_t b = 0; b < pressed.size(); b++)
                {
                    if (!pressed[b])
                        continue;
                    if (candidate_ >= 0 && (int(b) == candidate_ || int(b) == aButton))
                        return Keep(axes);
                    candidate_ = int(b);
                }
                return Event::None;
            }
            Fold(axes);
            if (bButton >= 0 && size_t(bButton) < pressed.size() && pressed[size_t(bButton)])
                return Stop();
            if (aButton >= 0 && size_t(aButton) < pressed.size() && pressed[size_t(aButton)] && Watch().found)
                return Keep(axes);
            return Event::None;
        }

        Found Watch() const
        {
            Found f;
            if (!running_)
                return f;
            if (IsButtonStep(step_))
            {
                f.found = candidate_ >= 0;
                f.button = candidate_;
                return f;
            }
            int32_t widest = -1;
            for (size_t a = 0; a < dist_.size(); a++)
                if (dist_[a] > widest)
                {
                    widest = dist_[a];
                    f.axis = int(a);
                }
            if (widest < kCalibrateMoved)
                return Found{};
            f.found = true;
            f.rest = int16_t(ref_[size_t(f.axis)]);
            f.full = int16_t(far_[size_t(f.axis)]);
            const float span = RestsCentred(f.rest) ? float(kAxisMax) : float(2 * kAxisMax);
            f.moved = std::clamp(float(widest) / span, 0.0f, 1.0f);
            return f;
        }

        // A from the keyboard or a controller (the wheel's own comes through
        // Update): keep what the step found and go on. None while it has
        // nothing yet, and the step carries on.
        Event Keep(std::span<const int16_t> axes)
        {
            const Found f = Watch();
            if (!running_ || !f.found)
                return Event::None;
            results_[size_t(step_)] = f;
            last_ = step_;
            if (IsSteerStep(step_))
                steerAxis_ = f.axis;
            Next(axes);
            return Event::Kept;
        }

        // On without keeping anything for this step (what was stored before
        // stays): a wheel without a clutch, or a control it has no button for.
        Event Skip(std::span<const int16_t> axes)
        {
            if (!running_)
                return Event::None;
            last_ = step_;
            Next(axes);
            return Event::Skipped;
        }

        // The step before, again.
        Event Back(std::span<const int16_t> axes)
        {
            if (!running_)
                return Event::None;
            if (step_ != Step::SteerLeft)
                step_ = Step(int(step_) - 1);
            last_ = step_;
            results_[size_t(step_)] = {};
            BeginStep(axes);
            return Event::Back;
        }

        Event Stop()
        {
            if (!running_)
                return Event::None;
            last_ = step_;
            running_ = false;
            return Event::Stopped;
        }

    private:
        void Next(std::span<const int16_t> axes)
        {
            if (size_t(step_) + 1 >= kStepCount)
            {
                running_ = false;
                return;
            }
            step_ = Step(int(step_) + 1);
            BeginStep(axes);
        }

        // An axis an earlier step of this run kept is measured from where it
        // rested then, and only the other way: Full right from Full left's
        // centre, away from the left end; a pedal sharing an earlier pedal's
        // axis from that pedal's rest, away from its press.
        void FromEarlier(Step earlierStep)
        {
            const Found& earlier = results_[size_t(earlierStep)];
            if (!earlier.found || earlier.axis < 0 || size_t(earlier.axis) >= ref_.size())
                return;
            const size_t a = size_t(earlier.axis);
            ref_[a] = far_[a] = earlier.rest;
            forbid_[a] = earlier.full > earlier.rest ? 1 : -1;
            fromFirst_[a] = false;
        }

        void BeginStep(std::span<const int16_t> axes)
        {
            candidate_ = -1;
            const size_t n = axes.size();
            ref_.assign(n, 0);
            far_.assign(n, 0);
            dist_.assign(n, -1);
            forbid_.assign(n, 0);
            allowed_.assign(n, true);
            fromFirst_.assign(n, false);
            for (size_t a = 0; a < n; a++)
            {
                ref_[a] = far_[a] = axes[a];
                fromFirst_[a] = a >= seen_.size() || !seen_[a];  // its 0 means nothing yet
            }
            if (step_ == Step::SteerRight && results_[size_t(Step::SteerLeft)].found)
            {
                for (size_t a = 0; a < n; a++)
                    allowed_[a] = int(a) == results_[size_t(Step::SteerLeft)].axis;
                FromEarlier(Step::SteerLeft);
            }
            if (IsPedalStep(step_))
            {
                if (steerAxis_ >= 0 && size_t(steerAxis_) < n)
                    allowed_[size_t(steerAxis_)] = false;
                for (int slot = 0; slot < PedalSlot(step_); slot++)
                    FromEarlier(PedalStep(slot));
            }
        }

        void Fold(std::span<const int16_t> axes)
        {
            for (size_t a = 0; a < dist_.size() && a < axes.size(); a++)
            {
                if (!allowed_[a] || fromFirst_[a] || a >= seen_.size() || !seen_[a])
                    continue;
                const int32_t d = int32_t(axes[a]) - ref_[a];
                if (d == 0 || (forbid_[a] > 0 && d > 0) || (forbid_[a] < 0 && d < 0))
                    continue;
                const int32_t m = d < 0 ? -d : d;
                if (m > dist_[a])
                {
                    dist_[a] = m;
                    far_[a] = axes[a];
                }
            }
        }

        bool running_ = false;
        Step step_ = Step::SteerLeft, last_ = Step::SteerLeft;
        int steerAxis_ = -1;
        std::array<Found, kStepCount> results_{};
        std::array<bool, kCalibrateButtons> prev_{};
        uint8_t prevHat_ = 0;
        int candidate_ = -1;
        std::vector<bool> seen_;
        std::vector<int32_t> ref_, far_, dist_;
        std::vector<int> forbid_;       // +1: nothing above ref counts, -1: nothing below
        std::vector<bool> allowed_;     // may be this step's axis at all
        std::vector<bool> fromFirst_;   // hadn't reported when the step began: ref comes from FirstValue
    };

    // An axis read the other way round (Settings > Wheel > Invert
    // Steering, or a wheel whose axis runs the opposite way): -32768 has no
    // positive twin, so it clamps to the end of the other side instead of
    // wrapping back to itself.
    constexpr int16_t Flip(int16_t raw)
    {
        return int16_t(-std::clamp(int(raw), -kAxisMax, kAxisMax));
    }

    // The wheel as the game's left stick, -1 (full left) to 1, from the
    // turn as a fraction of full lock (Steering::Fraction).
    //
    //   - `range` degrees of the wheel's turn reach full lock, out of the
    //     `full` degrees it turns altogether (either 0: all of it). We can't
    //     tell the wheel itself to turn less - that needs the maker's own
    //     driver - so a smaller range means full lock comes up before the
    //     wheel's stop does.
    //   - `deadzone` of the turn either side of centre reads as straight
    //     ahead, and the rest is rescaled so it still reaches full lock.
    //   - `response` bends what's left: 1 is straight through, above 1 is
    //     gentler near the centre and sharper near full lock, below 1 the
    //     other way round.
    inline float Steer(float turn, int range, int full, float deadzone, float response)
    {
        float x = std::clamp(turn, -1.0f, 1.0f);
        const float sign = x < 0 ? -1.0f : 1.0f;
        float m = std::abs(x);
        deadzone = std::clamp(deadzone, 0.0f, 0.95f);
        if (m <= deadzone)
            return 0.0f;
        m = (m - deadzone) / (1.0f - deadzone);
        if (range > 0 && full > 0 && range < full)
            m = m * float(full) / float(range);
        m = std::min(1.0f, m);
        if (response > 0.0f && response != 1.0f)
            m = std::pow(m, response);
        return sign * std::clamp(m, 0.0f, 1.0f);
    }

    // The game's own steering deadzone. The game was made for a pad, and the
    // player's car ignores the left stick within 20% of its centre, then
    // steers (|x| - 0.2) / 0.8 of full lock: measured in a race (round 2 of
    // the G29 test, NOTES.md), the stick swept in 1% steps and the car's
    // steering input read back each frame. Steering follows the stick at
    // once (no ramp in between); its arithmetic on the way:
    //   - XInput's -32768..32767 becomes an int, -1000..1000 (sub_8261BBA0:
    //     (s + 32768) * 2000 / 65535 - 1000, rounded down), GameStickUnits;
    //   - that, as a fraction, the 20% deadzone and the rescale, GameSteer.
    // On a wheel it was 54 degrees of a G29's turn (20% of 270 at Steering
    // Range 540) with the car going straight: the first thing a G29
    // test found.
    constexpr float kGameSteerDeadzone = 0.2f;
    constexpr int kGameStickUnits = 1000;     // the int's full scale
    constexpr int kGameDeadzoneUnits = 200;   // ...and the deadzone in it
    constexpr int kGameSteerSteps = kGameStickUnits - kGameDeadzoneUnits;  // what the car's steering tells apart

    constexpr int GameStickUnits(int16_t stick)
    {
        return int((int64_t(stick) + 32768) * 2000 / 65535) - kGameStickUnits;
    }

    inline float GameSteer(int16_t stick)
    {
        const int units = GameStickUnits(stick);
        const float m = float(units < 0 ? -units : units) / float(kGameStickUnits);
        if (m <= kGameSteerDeadzone)
            return 0.0f;
        return (units < 0 ? -1.0f : 1.0f) * (m - kGameSteerDeadzone) / (1.0f - kGameSteerDeadzone);
    }

    // The left stick (as hid/input.cpp's ToShort reads a fraction: times
    // 32767, towards zero) that makes the game steer `steer` (-1 to 1): its
    // deadzone stepped over, so the car turns from the wheel's first degree
    // and follows it to full lock. The game tells 800 steps apart past its
    // deadzone (a G29 at Steering Range 360: a quarter of a degree each); a
    // turn of less than half of one is straight ahead, so a wheel resting a
    // hair off its centre doesn't steer, or creep through the menus.
    // A pad never comes here: its stick reaches the game as before.
    inline float StickPastGameDeadzone(float steer)
    {
        const float y = std::clamp(steer, -1.0f, 1.0f);
        const int steps = int(std::lround(std::abs(y) * float(kGameSteerSteps)));
        if (steps == 0)
            return 0.0f;
        const int sign = y < 0 ? -1 : 1;
        const int units = sign * (kGameDeadzoneUnits + steps);
        // The smallest stick the game reads as those units: GameStickUnits
        // rounds down, so the first stick at or past (units + 1000) / 2000
        // of the way from -32768.
        const int64_t num = int64_t(units + kGameStickUnits) * 65535;
        const int stick = int(std::clamp<int64_t>((num + 1999) / 2000 - 32768, -32768, 32767));
        // ...as a fraction ToShort turns back into exactly that stick.
        return (float(stick) + 0.5f * float(sign)) / 32767.0f;
    }

    // Has the wheel been used? The game's idle check (SecondsSinceInput, which
    // v0.1.7's refresh-rate notice waits on for 3 s) counts a control away from
    // zero as in use. That suits a stick, whose deadzone makes one at rest
    // exactly 0; a wheel has none (Steering Deadzone starts at 0%) and rests a
    // little off its measured centre, so it was never idle and the notice never
    // came. A wheel is in use when it has turned since it was last counted, by
    // more than jitter.
    constexpr float kWheelTurned = 0.02f;  // of full lock

    struct WheelStill
    {
        float at = 0.0f;
        bool started = false;

        bool Turned(float steer)
        {
            if (started && std::abs(steer - at) <= kWheelTurned)
                return false;
            const bool was = started;
            started = true;
            at = steer;
            return was;  // where it first is says nothing
        }
    };

    // What the wheel's buttons and its hat amount to. The hat is the D-pad;
    // the paddles push the right stick up and down, which is how the game
    // shifts gears.
    struct Pressed
    {
        uint16_t buttons = 0;
        int16_t rightY = 0;
    };

    constexpr Pressed Resolve(std::span<const bool> buttons, uint8_t hatBits, std::span<const Binding> map)
    {
        Pressed out;
        bool up = false, down = false;
        for (const Binding& b : map)
        {
            if (b.index < 0 || size_t(b.index) >= buttons.size() || !buttons[size_t(b.index)])
                continue;
            switch (b.role)
            {
            case Role::Button: out.buttons |= b.button; break;
            case Role::ShiftUp: up = true; break;
            case Role::ShiftDown: down = true; break;
            }
        }
        // Both paddles at once is no gear change either way (as holding
        // both keys of a stick's pair centres it: hid/key_map.h).
        out.rightY = int16_t(up == down ? 0 : up ? kAxisMax : -kAxisMax);
        if (hatBits & hat::Up)
            out.buttons |= button::DpadUp;
        if (hatBits & hat::Down)
            out.buttons |= button::DpadDown;
        if (hatBits & hat::Left)
            out.buttons |= button::DpadLeft;
        if (hatBits & hat::Right)
            out.buttons |= button::DpadRight;
        return out;
    }

    // A pad control's name in NFSMW_WHEEL_BUTTONS, and in the log line that
    // says what a wheel's buttons do.
    inline constexpr std::pair<std::string_view, Binding> kNames[] = {
        { "A", { 0, button::A } }, { "B", { 0, button::B } }, { "X", { 0, button::X } }, { "Y", { 0, button::Y } },
        { "LB", { 0, button::LeftShoulder } }, { "RB", { 0, button::RightShoulder } },
        { "BACK", { 0, button::Back } }, { "START", { 0, button::Start } },
        { "LS", { 0, button::LeftThumb } }, { "RS", { 0, button::RightThumb } },
        { "UP", { 0, button::DpadUp } }, { "DOWN", { 0, button::DpadDown } },
        { "LEFT", { 0, button::DpadLeft } }, { "RIGHT", { 0, button::DpadRight } },
        { "SHIFTUP", { 0, 0, Role::ShiftUp } }, { "SHIFTDOWN", { 0, 0, Role::ShiftDown } },
        { "NONE", { 0, 0 } },
    };

    // What a Binding does, for the log.
    inline std::string_view NameOf(const Binding& b)
    {
        for (const auto& [name, binding] : kNames)
            if (binding.role == b.role && (b.role != Role::Button || binding.button == b.button))
                return name;
        return "?";
    }

    // NFSMW_WHEEL_BUTTONS="0=A,2=B,8=BACK,9=START,4=SHIFTUP": what a wheel's
    // buttons do, for a wheel whose numbering we have wrong (the log's
    // "[wheel] button n" lines say which is which). Every button named
    // replaces what the model gave it; NONE takes one out, and the rest of
    // the model's bindings stay. Returns the bindings, and in `error` the
    // first item it couldn't read ("" when all of them were fine).
    inline std::vector<Binding> ParseBindings(std::string_view text, std::span<const Binding> from, std::string& error)
    {
        std::vector<Binding> out(from.begin(), from.end());
        error.clear();
        while (!text.empty())
        {
            const size_t comma = text.find(',');
            std::string_view item = text.substr(0, comma);
            text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
            while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front())))
                item.remove_prefix(1);
            while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back())))
                item.remove_suffix(1);
            if (item.empty())
                continue;
            const size_t equals = item.find('=');
            if (equals == std::string_view::npos || equals == 0)
            {
                if (error.empty())
                    error = std::string(item);
                continue;
            }
            int index = 0;
            bool digits = true;
            for (char c : item.substr(0, equals))
                if (std::isdigit(static_cast<unsigned char>(c)))
                    index = std::min(256, index * 10 + (c - '0'));  // past 255 is refused below, never past an int
                else
                    digits = false;
            std::string name;
            for (char c : item.substr(equals + 1))
                name += char(std::toupper(static_cast<unsigned char>(c)));
            const Binding* found = nullptr;
            for (const auto& [known, binding] : kNames)
                if (known == name)
                    found = &binding;
            if (!digits || index > 255 || !found)
            {
                if (error.empty())
                    error = std::string(item);
                continue;
            }
            std::erase_if(out, [&](const Binding& b) { return b.index == index; });
            if (name != "NONE")
                out.push_back({ index, found->button, found->role });
        }
        std::stable_sort(out.begin(), out.end(), [](const Binding& a, const Binding& b) { return a.index < b.index; });
        return out;
    }

    // Settings > Wheel > Force Feedback: the rumble the game asks for
    // (XInput's two motor speeds) as one strength, 0 to 1, at `percent` of
    // full. The stronger motor has it: the game uses the two together for
    // one knock.
    inline float RumbleStrength(uint16_t low, uint16_t high, int percent)
    {
        const float strength = float(std::max(low, high)) / 65535.0f;
        return std::clamp(strength * float(std::clamp(percent, 0, 100)) / 100.0f, 0.0f, 1.0f);
    }

    // A knock as the wheel plays it: one sine of the wheel's own, started
    // when the game starts to rumble, its strength changed in place while it
    // plays (it is never restarted), and let go of when the game stops.
    //
    // SDL's rumble (SDL_PlayHapticRumble) is the wrong shape for a wheel. On
    // the Logitech wheels (SDL 3.4's SDL_hidapihaptic_lg4ff.c) it is a sine
    // with a 1 s period that each call starts again from 0 degrees, and the
    // game changes its rumble every frame, so the wheel only ever got the
    // first 6 degrees of it: a push to one side of a tenth of the strength
    // asked for, dropped to nothing at the next frame. The game knocks with
    // a single frame of full rumble (both motors, 32767 to 65535, then back
    // to 0: wall hits, bumps, gear changes), which came out as a 4% push
    // for 10 ms - the click. Changing the sine in place instead keeps where
    // it is in its swing (the driver carries the phase over an update).
    //
    // The shape (round 3 of the G29 test: the clicking came back while
    // driving). The game's rumble while scraping a wall or driving off the
    // road swings between ~25% and 100% a few times a second, with a frame
    // of jitter on top. Through a simulation of SDL's driver (its 2 ms
    // timer, ported), build 284's 75 ms sine at full strength, which took
    // each rise in one frame and started again on a new knock, reversed the
    // wheel's force ~15 times a second and jolted it (a 2 ms change of
    // slope over 8% of full force) ~12 times a second: a rattle through
    // the G29's gears. Now a 150 ms sine (fewer reversals), rising over at
    // least kKnockAttackMs (no jolts), at kKnockGain of the wheel's force:
    // ~7 reversals and under 0.5 jolts a second on the same rumble.
    constexpr int kKnockPeriodMs = 150;    // ~6.7 Hz: a thump, not a rattle
    constexpr int kKnockAttackMs = 33;     // from nothing to full strength (two frames)
    constexpr int kKnockReleaseMs = 200;   // from full strength to nothing
    constexpr float kKnockGain = 0.6f;     // a full rumble at Force Feedback 100%: 60% of the wheel's force
    constexpr float kKnockStep = 1.0f / 32.0f;  // smaller changes wait (each one costs the sine up to 2 ms of phase)
    // Long rumbles (the G29 retest of build 287): nitrous makes the game ask
    // for 3.5-4.4 s of rumble at 75-100%, which as a knock shook the rim the
    // whole time. A knock is short (a crash, a bump: a frame to a few hundred
    // ms); a rumble the game keeps asking for past kKnockSustainMs eases, at
    // the release, to kKnockSustainScale of what it asks, until it has been
    // quiet for kKnockSustainGapMs.
    constexpr int kKnockSustainMs = 300;       // longer than this is a rumble, not a knock
    constexpr float kKnockSustainScale = 0.15f;  // a long rumble: 15% of the knock's strength
    constexpr int kKnockSustainGapMs = 100;    // quiet this long: the next one is a knock again

    struct Knock
    {
        enum class Action { None, Start, Change, Stop };
        float level = 0.0f;  // what the wheel plays now, 0 to 1 (of a full rumble)
        float sent = 0.0f;   // what it was last told
        bool playing = false;
        float heldMs = 0.0f;   // how long the game has kept asking (gaps under kKnockSustainGapMs included)
        float quietMs = 0.0f;  // how long it has asked for nothing
        bool sustained = false;  // past kKnockSustainMs: played at kKnockSustainScale

        // Once a frame: the strength the game asks for now (RumbleStrength)
        // and the milliseconds since the last frame. Start: run the sine at
        // `level`, from the start of its swing; Change: give it `level`
        // without restarting it; Stop: stop it (it is at 0 by then).
        Action Step(float target, float ms)
        {
            target = std::clamp(target, 0.0f, 1.0f);
            ms = std::max(ms, 0.0f);
            if (target > 0.0f)
            {
                quietMs = 0.0f;
                heldMs += ms;
                sustained = sustained || heldMs > float(kKnockSustainMs);
            }
            else if ((quietMs += ms) >= float(kKnockSustainGapMs))
            {
                heldMs = 0.0f;
                sustained = false;
            }
            if (sustained)
                target *= kKnockSustainScale;
            if (target >= level)
                level = std::min(target, level + ms / float(kKnockAttackMs));
            else
                level = std::max(target, level - ms / float(kKnockReleaseMs));
            if (!playing)
            {
                if (level <= 0.0f)
                    return Action::None;
                playing = true;
                sent = level;
                return Action::Start;
            }
            if (level <= 0.0f)
            {
                playing = false;
                sent = 0.0f;
                heldMs = 0.0f;
                sustained = false;
                return Action::Stop;
            }
            if (std::abs(level - sent) >= kKnockStep || (level == target && level != sent))
            {
                sent = level;
                return Action::Change;
            }
            return Action::None;
        }

        // The sine's magnitude for `level` (SDL_HapticPeriodic's 0 to 32767).
        static int16_t Magnitude(float level)
        {
            return int16_t(std::lround(std::clamp(level, 0.0f, 1.0f) * kKnockGain * 32767.0f));
        }
    };

    // When the wheel is told its centring force (Settings > Wheel >
    // Centering Force): once when it opens, again when the setting changes,
    // and once more when the device first reports anything, because SDL's
    // Logitech driver sets the wheel's autocentre to 0 on its first report
    // (SDL_hidapi_lg4ff.c, with the range). Never on a timer: SDL says an
    // autocentre level by switching the wheel's centring off, setting it and
    // switching it on (SDL_hidapihaptic_lg4ff.c: 0xf5, 0xfe 0x0d, 0x14), and
    // builds up to 286 said it again every second while no knock played. The
    // spring let go for a moment each time, which can't be felt with the
    // wheel straight, and is a click a second with it held round a corner
    // (round 4 of the G29 test).
    struct Centring
    {
        int said = -1;          // the level the wheel was last told; -1: none yet
        bool reported = false;  // the device's first report has been answered
        int sends = 0;          // how many times it was told, since the wheel opened

        // Once a frame: the setting, and whether the device has reported
        // anything yet. True: tell the wheel `percent` now.
        bool Due(int percent, bool deviceReported)
        {
            percent = std::clamp(percent, 0, 100);
            bool due = percent != said;
            if (deviceReported && !reported)
            {
                reported = true;
                due = true;  // SDL's driver has just cleared it
            }
            if (due)
            {
                said = percent;
                sends++;
            }
            return due;
        }
    };

    // Settings > Wheel > Centering Force as a spring effect's coefficient
    // (how fast the force grows away from the middle, 0 to 0x7FFF) and its
    // saturation (how far it grows, 0 to 0xFFFF), for a wheel whose force
    // feedback takes a spring rather than an autocentre level.
    inline uint16_t SpringLevel(int percent)
    {
        return uint16_t(std::clamp(percent, 0, 100) * 0x7FFF / 100);
    }

    inline uint16_t SpringSaturation(int percent)
    {
        return uint16_t(std::clamp(percent, 0, 100) * 0xFFFF / 100);
    }
}
