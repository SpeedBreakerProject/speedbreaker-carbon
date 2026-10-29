// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See wheel.h.
#include "wheel.h"
#include "key_map.h"
#include "wheel_map.h"

#include <user/settings.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>
#include <vector>

#include <SDL3/SDL_haptic.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_timer.h>

// wheel_map.h names XInput's bits as literals (it has no kernel header), as
// key_map.h does; input.cpp checks key_map.h's against the bits the game
// reads, so agreeing with it is enough.
static_assert(hid::wheelmap::button::DpadUp == hid::keymap::button::DpadUp &&
    hid::wheelmap::button::DpadDown == hid::keymap::button::DpadDown &&
    hid::wheelmap::button::DpadLeft == hid::keymap::button::DpadLeft &&
    hid::wheelmap::button::DpadRight == hid::keymap::button::DpadRight &&
    hid::wheelmap::button::Start == hid::keymap::button::Start && hid::wheelmap::button::Back == hid::keymap::button::Back &&
    hid::wheelmap::button::LeftThumb == hid::keymap::button::LeftThumb &&
    hid::wheelmap::button::RightThumb == hid::keymap::button::RightThumb &&
    hid::wheelmap::button::LeftShoulder == hid::keymap::button::LeftShoulder &&
    hid::wheelmap::button::RightShoulder == hid::keymap::button::RightShoulder &&
    hid::wheelmap::button::A == hid::keymap::button::A && hid::wheelmap::button::B == hid::keymap::button::B &&
    hid::wheelmap::button::X == hid::keymap::button::X && hid::wheelmap::button::Y == hid::keymap::button::Y,
    "wheel_map.h's button bits are key_map.h's, and so XInput's");
// ...and its hat bits are SDL's.
static_assert(hid::wheelmap::hat::Centred == SDL_HAT_CENTERED && hid::wheelmap::hat::Up == SDL_HAT_UP &&
    hid::wheelmap::hat::Right == SDL_HAT_RIGHT && hid::wheelmap::hat::Down == SDL_HAT_DOWN &&
    hid::wheelmap::hat::Left == SDL_HAT_LEFT, "wheel_map.h's hat bits are SDL's");

namespace hid::wheel
{
    namespace
    {
        using wheelmap::Axes;
        using wheelmap::Binding;

        // A device reports at most this many buttons to us; the wheels we
        // know report 25 (a G29 with a shifter attached).
        constexpr int kMaxButtons = 64;
        static_assert(size_t(kMaxButtons) == wheelmap::kCalibrateButtons, "Calibrate watches every button read");

        bool s_started = false;
        SDL_Joystick* s_joystick = nullptr;
        SDL_JoystickID s_id = 0;
        std::string s_name;
        const wheelmap::Model* s_model = nullptr;
        std::vector<Binding> s_bindings;
        Axes s_axes;
        wheelmap::Steering s_steering;  // the wheel's two ends, measured or assumed
        wheelmap::Pedal s_pedals[3];  // in Step order: Throttle, Brake, Clutch
        int s_fullTurn = 0;           // degrees lock to lock, 0: unknown
        // The axes' resting places are read again on the first frame after
        // the device was opened: a joystick SDL has not polled yet reads 0
        // on every axis, and a pedal taken to rest at 0 when it really
        // rests at one end would hold the throttle down.
        bool s_restAgain = false;
        // Which axes SDL has reported a value for. It sends an axis only
        // when it moves, so everything reads 0 until something does: a
        // reading from an axis that has never spoken means nothing.
        std::vector<bool> s_axisSeen;

        SDL_Haptic* s_haptic = nullptr;
        bool s_canAutocentre = false;  // the wheel takes an autocentre level
        bool s_canSpring = false;      // ...or a spring effect
        bool s_canHapticRumble = false;  // SDL's own rumble: only where the wheel can't play a sine
        bool s_canKnock = false;         // it plays a sine of ours: the knocks (wheel_map.h's Knock)
        bool s_canJoystickRumble = false;
        bool s_onlyOneEffect = false;  // a rumble would stop the centring: the centring wins
        SDL_HapticEffectID s_spring = -1;
        wheelmap::Centring s_centring;  // when the wheel is told its centring force (wheel_map.h)
        bool s_reported = false;        // the wheel has sent an event (SDL's driver has cleared its autocentre)
        Uint64 s_centringLogAt = 0;     // the per-minute count's last line (SDL ticks)
        int s_centringLogged = 0;       // ...and s_centring.sends then
        float s_rumble = 0.0f;         // what it was last told to play
        // The knocks (wheel_map.h's Knock): the sine, made at the first one
        // and kept, and what the one under way has done, for its log line.
        SDL_HapticEffectID s_knockEffect = -1;
        wheelmap::Knock s_knock;
        Uint64 s_knockFrameNs = 0;   // the last frame's time (SDL_GetTicksNS)
        Uint64 s_knockStartNs = 0;
        float s_knockPeak = 0.0f;
        int s_knockChanges = 0;
        bool s_knockSustained = false;  // this knock became a long rumble (eased)
        int s_knocks = 0;  // since the wheel opened: each one's log line has its number

        const bool s_log = std::getenv("NFSMW_WHEEL_LOG") != nullptr;

        // A pedal step's place in s_pedals, and back: slot 0 is the throttle,
        // 1 the brake, 2 the clutch (wheel_map.h; Step starts with the wheel's
        // two ends, so a slot is not a Step by itself).
        int Slot(Step step) { return wheelmap::PedalSlot(step); }
        using wheelmap::PedalStep;

        settings::Id AxisOption(Step step)
        {
            switch (step)
            {
            case Step::Throttle: return settings::Id::WheelThrottleAxis;
            case Step::Brake: return settings::Id::WheelBrakeAxis;
            case Step::Clutch: return settings::Id::WheelClutchAxis;
            default: return settings::Id::WheelSteerAxis;
            }
        }

        settings::Id RestOption(Step step)
        {
            switch (step)
            {
            case Step::Brake: return settings::Id::BrakeRest;
            case Step::Clutch: return settings::Id::ClutchRest;
            default: return settings::Id::ThrottleRest;
            }
        }

        settings::Id FullOption(Step step)
        {
            switch (step)
            {
            case Step::Brake: return settings::Id::BrakeFull;
            case Step::Clutch: return settings::Id::ClutchFull;
            default: return settings::Id::ThrottleFull;
            }
        }

        // The setting holding the button given to a pad control; they run in
        // Control order from ButtonA, as the static_assert below checks.
        settings::Id ButtonOption(wheelmap::Control control)
        {
            return settings::Id(size_t(settings::Id::ButtonA) + size_t(control));
        }
        static_assert(size_t(settings::Id::ButtonShiftDown) - size_t(settings::Id::ButtonA) + 1 == wheelmap::kControlCount,
            "one Button option per wheel_map.h Control, in Control order");

        int AxisOf(Step step)
        {
            switch (step)
            {
            case Step::Throttle: return s_axes.throttle;
            case Step::Brake: return s_axes.brake;
            case Step::Clutch: return s_axes.clutch;
            default: return s_axes.steer;
            }
        }

        // What Calibrate recorded for each control, in Control order.
        std::array<int, wheelmap::kControlCount> StoredButtons()
        {
            std::array<int, wheelmap::kControlCount> out{};
            for (size_t i = 0; i < wheelmap::kControlCount; i++)
                out[i] = settings::GetInt(ButtonOption(wheelmap::Control(i)));
            return out;
        }

        int16_t AxisNow(int axis)
        {
            if (!s_joystick || axis < 0)
                return 0;
            return SDL_GetJoystickAxis(s_joystick, axis);
        }

        // Where every axis is now, which is where they rest when the device
        // has just been opened.
        std::vector<int16_t> AxisStates(bool initial)
        {
            std::vector<int16_t> values;
            if (!s_joystick)
                return values;
            const int count = std::max(0, SDL_GetNumJoystickAxes(s_joystick));
            values.resize(size_t(count));
            for (int a = 0; a < count; a++)
            {
                Sint16 state = 0;
                // The state SDL saw when the device turned up, which a pedal
                // hasn't had a chance to move yet; failing that, now's.
                if (!initial || !SDL_GetJoystickAxisInitialState(s_joystick, a, &state))
                    state = SDL_GetJoystickAxis(s_joystick, a);
                values[size_t(a)] = state;
            }
            return values;
        }

        // The axes the player assigned (Settings > Wheel > Calibrate); -1 each
        // where they haven't.
        Axes StoredAxes()
        {
            return { settings::GetInt(settings::Id::WheelSteerAxis), settings::GetInt(settings::Id::WheelThrottleAxis),
                     settings::GetInt(settings::Id::WheelBrakeAxis), settings::GetInt(settings::Id::WheelClutchAxis) };
        }

        // Which axis is which, and each pedal's resting place: the model's
        // idea, checked against where the axes rest, with the player's own
        // assignments on top of both.
        void Calibrate(bool initial)
        {
            const std::vector<int16_t> rest = AxisStates(initial);
            bool anySeen = false;
            for (size_t a = 0; a < s_axisSeen.size(); a++)
                anySeen = anySeen || s_axisSeen[a];
            const Axes stored = StoredAxes();
            Axes preferred = s_model->axes;
            if (stored.steer >= 0) preferred.steer = stored.steer;
            if (stored.throttle >= 0) preferred.throttle = stored.throttle;
            if (stored.brake >= 0) preferred.brake = stored.brake;
            if (stored.clutch >= 0) preferred.clutch = stored.clutch;
            // Where the axes rest only means something once they have
            // reported: before that, the model's own layout is all there is.
            s_axes = anySeen ? wheelmap::Detect(rest, preferred) : preferred;
            // The player's assignments are not guesses: they stand.
            if (stored.steer >= 0) s_axes.steer = stored.steer;
            if (stored.throttle >= 0) s_axes.throttle = stored.throttle;
            if (stored.brake >= 0) s_axes.brake = stored.brake;
            if (stored.clutch >= 0) s_axes.clutch = stored.clutch;
            // The wheel's two ends: both as Calibrate measured them, else
            // the axis as it comes. Both have to be there - one alone would
            // put the centre halfway to nowhere.
            const int16_t left = int16_t(settings::GetInt(settings::Id::SteerLeft));
            const int16_t right = int16_t(settings::GetInt(settings::Id::SteerRight));
            if (left != right && left != 0 && right != 0)
                s_steering.Start(left, right);
            else
                s_steering.Start(-wheelmap::kAxisMax, wheelmap::kAxisMax);
            // Each pedal's axis and the span it runs over: what Calibrate
            // measured, else what this wheel's model says its pedals do,
            // else the axis itself once it has reported (its far end being
            // as far as it can go). Without any of those it reads nothing,
            // rather than reading as pressed.
            struct Plan { int axis = -1; int16_t rest = 0, full = 0; bool known = false; };
            Plan plan[3];
            for (Step step : { Step::Throttle, Step::Brake, Step::Clutch })
            {
                Plan& it = plan[Slot(step)];
                it.axis = AxisOf(step);
                if (it.axis < 0 || size_t(it.axis) >= rest.size())
                {
                    it.axis = -1;
                    continue;
                }
                const int16_t storedRest = int16_t(settings::GetInt(RestOption(step)));
                const int16_t storedFull = int16_t(settings::GetInt(FullOption(step)));
                if (storedRest != storedFull)
                    it = { it.axis, storedRest, storedFull, true };
                else if (s_model->pedalRest != s_model->pedalFull)
                    it = { it.axis, s_model->pedalRest, s_model->pedalFull, true };
                else if (size_t(it.axis) < s_axisSeen.size() && s_axisSeen[size_t(it.axis)])
                {
                    const int16_t at = rest[size_t(it.axis)];
                    it = { it.axis, at, int16_t(at >= 0 ? -32768 : 32767), true };
                }
            }
            // No two controls may read one axis the same way: that is how a
            // calibration gone wrong has one pedal working two of the
            // game's, while the other pedal does nothing at all. Two pedals
            // sharing an axis is allowed only where their spans run
            // opposite ways, which is how a wheel with both on one axis
            // reports them. The wheel's own axis is never a pedal.
            for (int a = 0; a < 3; a++)
            {
                if (plan[a].axis >= 0 && plan[a].axis == s_axes.steer)
                {
                    fprintf(stderr, "[wheel] %s is on the wheel's own axis %d: ignored\n", StepName(PedalStep(a)), plan[a].axis);
                    plan[a] = {};
                }
                for (int b = a + 1; b < 3; b++)
                {
                    if (plan[a].axis < 0 || plan[a].axis != plan[b].axis)
                        continue;
                    const bool opposite = plan[a].known && plan[b].known &&
                        (plan[a].full > plan[a].rest) != (plan[b].full > plan[b].rest);
                    if (opposite)
                        continue;  // one axis, one each way: both are real
                    fprintf(stderr, "[wheel] %s and %s both read axis %d the same way: %s dropped (calibrate it)\n",
                        StepName(PedalStep(a)), StepName(PedalStep(b)), plan[a].axis, StepName(PedalStep(b)));
                    plan[b] = {};
                    settings::Set(AxisOption(PedalStep(b)), -1);  // so it doesn't come back next run
                }
            }
            for (Step step : { Step::Throttle, Step::Brake, Step::Clutch })
            {
                const Plan& it = plan[Slot(step)];
                s_pedals[Slot(step)] = {};  // not ready: reads nothing
                if (it.axis >= 0 && it.known)
                    s_pedals[Slot(step)].Start(it.rest, it.full);
            }
            s_axes.throttle = plan[0].axis;
            s_axes.brake = plan[1].axis;
            s_axes.clutch = plan[2].axis;
            // Each pedal's span as well as its axis: a pedal reading
            // nothing, or reading backwards, shows here before anyone has
            // to guess at it.
            std::string pedals;
            for (Step step : { Step::Throttle, Step::Brake, Step::Clutch })
            {
                const wheelmap::Pedal& it = s_pedals[Slot(step)];
                pedals += std::format(", {} {}", StepName(step), AxisOf(step));
                pedals += it.ready ? std::format(" ({:+d} to {:+d})", int(it.idle), int(it.full)) : std::string(" (not placed)");
            }
            fprintf(stderr, "[wheel] axes: steering %d%s (of %zu; rest", s_axes.steer, pedals.c_str(), rest.size());
            for (int16_t v : rest)
                fprintf(stderr, " %d", int(v));
            fprintf(stderr, ")\n");
        }

        // The knock under way, in one line when it ends (every one: what the
        // wheel did is what a tester's report is about). "up to" is the
        // strength of a full rumble (Force Feedback applied), "force" the
        // share of the wheel's own that the sine swung to.
        void LogKnock(const char* how)
        {
            if (s_knockStartNs == 0)
                return;
            fprintf(stderr, "[wheel] knock %d: %.0f ms, up to %.0f%% (force %.0f%%, Force Feedback %d%%), %d changes%s, %s\n",
                s_knocks, double(SDL_GetTicksNS() - s_knockStartNs) / 1e6, double(s_knockPeak) * 100.0,
                double(s_knockPeak * wheelmap::kKnockGain) * 100.0, settings::GetInt(settings::Id::ForceFeedback), s_knockChanges,
                s_knockSustained ? ", eased as a long rumble" : "", how);
        }

        void EndKnock(const char* how)
        {
            if (!s_knock.playing && s_knockStartNs == 0)
                return;
            if (s_haptic && s_knockEffect >= 0)
                SDL_StopHapticEffect(s_haptic, s_knockEffect);
            LogKnock(how);
            s_knock = {};
            s_knockStartNs = 0;
            s_knockSustained = false;
        }

        // A frame of the knocks (wheel_map.h's Knock): the strength the game
        // asks for now, at Settings > Wheel > Force Feedback.
        void PlayKnock(float strength)
        {
            const Uint64 now = SDL_GetTicksNS();
            const float ms = s_knockFrameNs ? float(double(now - s_knockFrameNs) / 1e6) : 0.0f;
            s_knockFrameNs = now;
            using Action = wheelmap::Knock::Action;
            // (A long frame - a hitch, the menu - lets go of no more than 100 ms' worth.)
            const bool wasSustained = s_knock.sustained;
            const Action action = s_knock.Step(strength, std::min(ms, 100.0f));
            if (s_knock.sustained && !wasSustained)
            {
                s_knockSustained = true;
                fprintf(stderr, "[wheel] knock %d: a long rumble (asked for over %d ms, %.0f%% now): eased to %.0f%% of it\n",
                    s_knocks, wheelmap::kKnockSustainMs, double(strength) * 100.0, double(wheelmap::kKnockSustainScale) * 100.0);
            }
            if (action == Action::None)
                return;
            if (action == Action::Stop)
            {
                EndKnock("let go");
                return;
            }
            SDL_HapticEffect effect{};
            effect.periodic.type = SDL_HAPTIC_SINE;
            effect.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
            effect.periodic.direction.dir[0] = 1;  // along the wheel's one axis
            effect.periodic.period = wheelmap::kKnockPeriodMs;
            effect.periodic.magnitude = wheelmap::Knock::Magnitude(s_knock.level);
            effect.periodic.length = SDL_HAPTIC_INFINITY;  // it plays until the knock is let go of
            if (s_log)
                fprintf(stderr, "[wheel] knock %s: magnitude %d\n", action == Action::Start ? "starts" : "changes",
                    int(effect.periodic.magnitude));
            if (action == Action::Change)
            {
                // In place: the driver keeps the sine where it is in its swing.
                if (SDL_UpdateHapticEffect(s_haptic, s_knockEffect, &effect))
                {
                    s_knockChanges++;
                    s_knockPeak = std::max(s_knockPeak, s_knock.level);
                }
                return;
            }
            // Start: made at the first knock and kept; each knock runs it from
            // the start of its swing, where it pushes from nothing.
            bool ok = s_knockEffect >= 0 ? SDL_UpdateHapticEffect(s_haptic, s_knockEffect, &effect)
                                         : (s_knockEffect = SDL_CreateHapticEffect(s_haptic, &effect)) >= 0;
            ok = ok && SDL_RunHapticEffect(s_haptic, s_knockEffect, 1);
            if (!ok)
            {
                fprintf(stderr, "[wheel] knock failed: %s; SDL's rumble instead from now on\n", SDL_GetError());
                if (s_knockEffect >= 0)
                    SDL_DestroyHapticEffect(s_haptic, s_knockEffect);
                s_knockEffect = -1;
                s_knock = {};
                s_canKnock = false;
                s_canHapticRumble = SDL_InitHapticRumble(s_haptic);
                return;
            }
            s_knockStartNs = now;
            s_knockPeak = s_knock.level;
            s_knockChanges = 0;
            s_knockSustained = false;
            s_knocks++;
        }

        void StopRumble()
        {
            s_rumble = 0.0f;
            EndKnock("stopped");
            if (s_haptic && s_canHapticRumble)
                SDL_StopHapticRumble(s_haptic);
            if (s_joystick && s_canJoystickRumble)
                SDL_RumbleJoystick(s_joystick, 0, 0, 0);
        }

        void StopSpring()
        {
            if (s_haptic && s_spring >= 0)
            {
                SDL_StopHapticEffect(s_haptic, s_spring);
                SDL_DestroyHapticEffect(s_haptic, s_spring);
            }
            s_spring = -1;
        }

        // Settings > Wheel > Centering Force: an autocentre level where the
        // wheel takes one, else a spring effect that pulls it to the middle,
        // said only when wheel_map.h's Centring says it is due (never on a
        // timer: each time, SDL lets the wheel's centring go for a moment).
        void ApplyCentering()
        {
            if (!s_haptic)
                return;
            const int percent = std::clamp(settings::GetInt(settings::Id::CenteringForce), 0, 100);
            if (!s_centring.Due(percent, s_reported))
                return;
            if (s_log)
                fprintf(stderr, "[wheel] centring %d%% said (%d since the wheel opened)\n", percent, s_centring.sends);
            if (s_canAutocentre)
            {
                if (!SDL_SetHapticAutocenter(s_haptic, percent))
                    fprintf(stderr, "[wheel] autocentre %d%% failed: %s\n", percent, SDL_GetError());
                return;
            }
            if (!s_canSpring)
                return;
            if (percent == 0)
            {
                StopSpring();
                return;
            }
            SDL_HapticEffect effect{};
            effect.condition.type = SDL_HAPTIC_SPRING;
            effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
            effect.condition.direction.dir[0] = 1;
            effect.condition.length = SDL_HAPTIC_INFINITY;
            for (int axis = 0; axis < 3; axis++)
            {
                effect.condition.right_sat[axis] = wheelmap::SpringSaturation(percent);
                effect.condition.left_sat[axis] = wheelmap::SpringSaturation(percent);
                effect.condition.right_coeff[axis] = Sint16(wheelmap::SpringLevel(percent));
                effect.condition.left_coeff[axis] = Sint16(wheelmap::SpringLevel(percent));
            }
            if (s_spring >= 0)
            {
                if (SDL_UpdateHapticEffect(s_haptic, s_spring, &effect))
                    return;
                StopSpring();  // it wouldn't take the change: make it afresh
            }
            s_spring = SDL_CreateHapticEffect(s_haptic, &effect);
            if (s_spring < 0 || !SDL_RunHapticEffect(s_haptic, s_spring, SDL_HAPTIC_INFINITY))
            {
                fprintf(stderr, "[wheel] centring force failed: %s\n", SDL_GetError());
                StopSpring();
                s_canSpring = false;
            }
        }

        // What the wheel can do with its motors, which needs the system to
        // drive them: on a Mac SDL 3.4's own Logitech driver
        // (SDL_hidapihaptic_lg4ff.c: a constant force, a sine, a spring, an
        // autocentre level and gain, 16 effects at once, as measured on a
        // G29), on Linux the kernel's driver (not tried yet).
        void OpenHaptics()
        {
            s_canAutocentre = s_canSpring = s_canHapticRumble = s_canKnock = s_onlyOneEffect = false;
            s_knockEffect = -1;
            s_knock = {};
            s_knockStartNs = 0;
            s_canJoystickRumble = false;
            s_centring = {};
            s_centringLogged = 0;
            s_centringLogAt = SDL_GetTicks();
            if (!s_joystick)
                return;
            SDL_PropertiesID props = SDL_GetJoystickProperties(s_joystick);
            s_canJoystickRumble = SDL_GetBooleanProperty(props, SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false);
            if (!SDL_IsJoystickHaptic(s_joystick))
            {
                fprintf(stderr, "[wheel] no force feedback on this system: the wheel stays slack%s\n",
                    s_canJoystickRumble ? " (it does take plain rumble)" : "");
                return;
            }
            s_haptic = SDL_OpenHapticFromJoystick(s_joystick);
            if (!s_haptic)
            {
                fprintf(stderr, "[wheel] force feedback didn't open: %s\n", SDL_GetError());
                return;
            }
            const Uint32 features = SDL_GetHapticFeatures(s_haptic);
            s_canAutocentre = (features & SDL_HAPTIC_AUTOCENTER) != 0;
            s_canSpring = (features & SDL_HAPTIC_SPRING) != 0;
            // Knocks are a sine of our own where the wheel plays one (the
            // Logitech wheels on a Mac); SDL's own rumble only where it
            // can't (wheel_map.h's Knock says why).
            s_canKnock = (features & SDL_HAPTIC_SINE) != 0;
            s_canHapticRumble = !s_canKnock && SDL_InitHapticRumble(s_haptic);
            // One effect at a time: a knock would take the centring force
            // away, which matters far more while driving.
            const int playing = SDL_GetMaxHapticEffectsPlaying(s_haptic);
            s_onlyOneEffect = playing == 1 && !s_canAutocentre && s_canSpring;
            const std::string sine = std::format("yes (our own {} ms sine at up to {:.0f}% of the wheel's force, rising over {} ms, "
                                                 "let go of over {} ms)",
                wheelmap::kKnockPeriodMs, double(wheelmap::kKnockGain) * 100.0, wheelmap::kKnockAttackMs, wheelmap::kKnockReleaseMs);
            fprintf(stderr, "[wheel] force feedback: centring %s, knocks %s\n",
                s_canAutocentre ? "autocentre" : s_canSpring ? "spring" : "no",
                s_onlyOneEffect ? "no (one effect at a time: the centring force has it)"
                : s_canKnock    ? sine.c_str()
                                : s_canHapticRumble ? "yes" : s_canJoystickRumble ? "rumble" : "no");
        }

        void Close()
        {
            StopRumble();
            StopSpring();
            if (s_haptic)
            {
                if (s_canAutocentre)
                    SDL_SetHapticAutocenter(s_haptic, 0);
                SDL_CloseHaptic(s_haptic);
                s_haptic = nullptr;
            }
            if (s_joystick)
            {
                SDL_CloseJoystick(s_joystick);
                s_joystick = nullptr;
            }
            s_id = 0;
            s_restAgain = false;
            s_axisSeen.clear();
            s_name.clear();
            s_bindings.clear();
            s_axes = {};
            s_model = nullptr;
            s_fullTurn = 0;
            s_centring = {};
            s_reported = false;
        }

        // NFSMW_WHEEL_BUTTONS="0=A,2=B,8=BACK,9=START,4=SHIFTUP": what the
        // wheel's buttons do, for one whose numbering the model has wrong.
        void TakeBindings()
        {
            // The model's buttons, with whatever Calibrate recorded on top.
            const std::array<int, wheelmap::kControlCount> assigned = StoredButtons();
            s_bindings = wheelmap::WithAssigned(s_model->buttons, assigned);
            const char* text = std::getenv("NFSMW_WHEEL_BUTTONS");
            if (!text || !*text)
                return;
            std::string error;
            s_bindings = wheelmap::ParseBindings(text, s_bindings, error);
            if (!error.empty())
                fprintf(stderr, "[wheel] NFSMW_WHEEL_BUTTONS: can't read \"%s\"\n", error.c_str());
            std::string shown;
            for (const Binding& b : s_bindings)
                shown += (shown.empty() ? "" : " ") + std::to_string(b.index) + "=" + std::string(wheelmap::NameOf(b));
            fprintf(stderr, "[wheel] buttons: %s\n", shown.c_str());
        }

        void Open()
        {
            if (s_joystick || !s_started)
                return;
            int count = 0;
            SDL_JoystickID* ids = SDL_GetJoysticks(&count);
            if (!ids)
                return;
            for (int i = 0; i < count && !s_joystick; i++)
            {
                const SDL_JoystickID id = ids[i];
                const char* name = SDL_GetJoystickNameForID(id);
                const bool sdlSaysWheel = SDL_GetJoystickTypeForID(id) == SDL_JOYSTICK_TYPE_WHEEL;
                const uint16_t vendor = SDL_GetJoystickVendorForID(id), product = SDL_GetJoystickProductForID(id);
                SDL_Joystick* joystick = SDL_OpenJoystick(id);
                if (!joystick)
                {
                    fprintf(stderr, "[wheel] joystick %u didn't open: %s\n", unsigned(id), SDL_GetError());
                    continue;
                }
                // How many axes and buttons it has takes an open device, and
                // tells a wheel from a gamepad whose name happens to match.
                if (!wheelmap::LooksLikeWheel(sdlSaysWheel, vendor, product, name ? name : "",
                        SDL_GetNumJoystickAxes(joystick), SDL_GetNumJoystickButtons(joystick)))
                {
                    SDL_CloseJoystick(joystick);
                    continue;
                }
                // The device has only just been opened: make sure SDL has
                // read it before its axes are taken for where they rest.
                SDL_UpdateJoysticks();
                s_joystick = joystick;
                s_id = id;
                s_name = name ? name : "Steering wheel";
                s_model = &wheelmap::ModelFor(vendor, product);
                s_fullTurn = s_model->fullTurnDegrees;
                const std::string turn = s_fullTurn ? std::to_string(s_fullTurn) + " degrees" : "an unknown amount";
                fprintf(stderr, "[wheel] %s (USB %04x:%04x, %s, %d axes, %d buttons, %d hats, turns %s)\n", s_name.c_str(),
                    vendor, product, s_model->vendor ? s_model->name : "not one we know",
                    SDL_GetNumJoystickAxes(s_joystick), SDL_GetNumJoystickButtons(s_joystick),
                    SDL_GetNumJoystickHats(s_joystick), turn.c_str());
                TakeBindings();
                s_axisSeen.assign(size_t(std::max(0, SDL_GetNumJoystickAxes(s_joystick))), false);
                Calibrate(true);
                s_restAgain = true;
                OpenHaptics();
                ApplyCentering();
            }
            SDL_free(ids);
        }

        // Settings > Wheel > Calibrate (wheel_map.h's rules; Sample feeds it).
        wheelmap::Calibration s_cal;

        // The joystick button that is this pad button now (-1: none).
        int JoystickButtonOf(uint16_t padButton)
        {
            for (const Binding& b : s_bindings)
                if (b.role == wheelmap::Role::Button && b.button == padButton)
                    return b.index;
            return -1;
        }

        // What a calibration step did: a kept step is stored at once, so
        // stopping (or the wheel going) keeps everything done so far.
        void ApplyCalibration(wheelmap::Calibration::Event event)
        {
            using Event = wheelmap::Calibration::Event;
            if (event == Event::None)
                return;
            const Step step = s_cal.LastStep();
            if (event == Event::Kept)
            {
                const wheelmap::Calibration::Found f = s_cal.LastFound();
                if (wheelmap::IsButtonStep(step))
                {
                    settings::Set(ButtonOption(wheelmap::ControlOf(step)), double(f.button));
                    TakeBindings();
                    fprintf(stderr, "[wheel] calibrate: %s: button %d\n", StepName(step), f.button);
                }
                else
                {
                    fprintf(stderr, "[wheel] calibrate: %s: axis %d, from %+d to %+d (%.0f%% of its travel)\n", StepName(step),
                        f.axis, int(f.rest), int(f.full), double(f.moved) * 100.0);
                    if (step == Step::SteerLeft)
                    {
                        settings::Set(settings::Id::WheelSteerAxis, double(f.axis));
                        settings::Set(settings::Id::SteerLeft, double(f.full));
                        // The two ends say which way round the wheel is, so
                        // the manual flip starts clear.
                        settings::Set(settings::Id::InvertSteering, 0.0);
                    }
                    else if (step == Step::SteerRight)
                    {
                        settings::Set(settings::Id::WheelSteerAxis, double(f.axis));
                        settings::Set(settings::Id::SteerRight, double(f.full));
                    }
                    else
                    {
                        settings::Set(AxisOption(step), double(f.axis));
                        settings::Set(RestOption(step), double(f.rest));
                        settings::Set(FullOption(step), double(f.full));
                    }
                    Calibrate(false);
                }
            }
            else if (event == Event::Skipped)
                fprintf(stderr, "[wheel] calibrate: %s skipped\n", StepName(step));
            else if (event == Event::Back)
                fprintf(stderr, "[wheel] calibrate: back to %s\n", StepName(step));
            else if (event == Event::Stopped)
                fprintf(stderr, "[wheel] calibrate: stopped at %s\n", StepName(step));
            if (!s_cal.Running() && event != Event::Stopped)
                fprintf(stderr, "[wheel] calibrate: done\n");
        }

        // NFSMW_WHEEL_LOG=1: every button as it is pressed (which is how to
        // find a wheel's numbering), and the axes once a second.
        void LogSample(const std::array<bool, kMaxButtons>& buttons, const Reading& r)
        {
            if (!s_log)
                return;
            static std::array<bool, kMaxButtons> was{};
            for (int b = 0; b < kMaxButtons; b++)
                if (buttons[size_t(b)] != was[size_t(b)])
                    fprintf(stderr, "[wheel] button %d %s\n", b, buttons[size_t(b)] ? "down" : "up");
            was = buttons;
            static auto last = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            if (now - last < std::chrono::seconds(1))
                return;
            last = now;
            fprintf(stderr, "[wheel] steering raw %d -> %+.3f (the game's stick %+.3f); pedals raw (%d, %d, %d) -> throttle %.2f, "
                            "brake %.2f, clutch %.2f\n",
                int(AxisNow(s_axes.steer)), r.steer, r.stick, int(AxisNow(s_axes.throttle)), int(AxisNow(s_axes.brake)),
                int(AxisNow(s_axes.clutch)), r.throttle, r.brake, r.clutch);
        }

        std::array<bool, kMaxButtons> ButtonsNow()
        {
            std::array<bool, kMaxButtons> held{};
            if (!s_joystick)
                return held;
            const int count = std::min(kMaxButtons, std::max(0, SDL_GetNumJoystickButtons(s_joystick)));
            for (int b = 0; b < count; b++)
                held[size_t(b)] = SDL_GetJoystickButton(s_joystick, b);
            return held;
        }

        uint8_t HatNow()
        {
            if (!s_joystick || SDL_GetNumJoystickHats(s_joystick) < 1)
                return wheelmap::hat::Centred;
            return SDL_GetJoystickHat(s_joystick, 0);
        }

        // How far a pedal is down, or nothing at all while its axis has yet
        // to report: until then the 0 SDL reads needn't be where the pedal
        // is. On a wheel whose
        // pedals run +32767 to -32768 that 0 is halfway, so without this a
        // resting pedal reads half pressed.
        float PedalValue(Step step)
        {
            const int axis = AxisOf(step);
            if (axis < 0 || size_t(axis) >= s_axisSeen.size() || !s_axisSeen[size_t(axis)])
                return 0.0f;
            return s_pedals[Slot(step)].Value(AxisNow(axis));
        }

        bool InPlay()
        {
            return s_joystick != nullptr && settings::GetBool(settings::Id::Wheel);
        }

        Reading s_last;  // what Sample last read (Last)
    }

    bool StepIsButton(Step step)
    {
        return wheelmap::IsButtonStep(step);
    }

    const char* StepName(Step step)
    {
        switch (step)
        {
        case Step::SteerLeft: return "Full left";
        case Step::SteerRight: return "Full right";
        case Step::Throttle: return "Throttle";
        case Step::Brake: return "Brake";
        case Step::Clutch: return "Clutch";
        default: break;
        }
        if (StepIsButton(step))
            return wheelmap::kControls[size_t(step) - size_t(Step::ButtonA)].name;
        return "?";
    }

    const char* StepAsk(Step step)
    {
        switch (step)
        {
        case Step::SteerLeft: return "Turn the wheel fully left";
        case Step::SteerRight: return "Turn the wheel fully right";
        case Step::Throttle: return "Press the throttle to the floor";
        case Step::Brake: return "Press the brake to the floor";
        case Step::Clutch: return "Press the clutch to the floor";
        default: break;
        }
        return StepIsButton(step) ? "Press the wheel button you want for it" : "?";
    }

    void Initialize()
    {
        s_started = true;
        // A Steering Response below 1.00 from a test build before the game's
        // own 20% deadzone was stepped over (settings_version 0) was set to
        // fight that deadzone, and now makes the centre far too sharp (0.50:
        // the first 10% of steering inside 3 degrees of the wheel). Back to
        // 1.00 once; the file takes it at the game's next save. A value set
        // since (version 1) is the player's.
        if (settings::GetInt(settings::Id::WheelSettingsVersion) < 1)
        {
            const float response = settings::GetFloat(settings::Id::SteeringResponse);
            if (response < 1.0f && !settings::EnvOverride(settings::Id::SteeringResponse))
            {
                settings::Set(settings::Id::SteeringResponse, 1.0);
                fprintf(stderr, "[wheel] Steering Response %.2f was set while the game's own steering deadzone was in the way: "
                                "back to 1.00, which now steers in step with the wheel\n",
                    double(response));
            }
            settings::Set(settings::Id::WheelSettingsVersion, 1.0);
        }
        // Every Wheel setting as it is read, so a run says for itself what
        // it is playing by: a value here that differs from settings.toml
        // means the file didn't reach the game.
        fprintf(stderr, "[wheel] settings: wheel %s, range %s, invert %s, deadzone %.2f, response %.2f, "
                        "force feedback %d%%, centring %d%%\n",
            settings::GetBool(settings::Id::Wheel) ? "on" : "off",
            settings::FormatValue(settings::Id::SteeringRange, settings::GetValue(settings::Id::SteeringRange)).c_str(),
            settings::GetBool(settings::Id::InvertSteering) ? "yes" : "no",
            double(settings::GetFloat(settings::Id::SteeringDeadzone)), double(settings::GetFloat(settings::Id::SteeringResponse)),
            settings::GetInt(settings::Id::ForceFeedback), settings::GetInt(settings::Id::CenteringForce));
        Open();
    }

    void HandleEvent(const SDL_Event& e)
    {
        if (!s_started)
            return;
        // The wheel's first event of any kind follows SDL's driver clearing
        // its autocentre (on the first report that changed anything): the
        // centring force is said once more (wheel_map.h Centring).
        if (s_joystick && !s_reported &&
            ((e.type == SDL_EVENT_JOYSTICK_AXIS_MOTION && e.jaxis.which == s_id) ||
                ((e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN || e.type == SDL_EVENT_JOYSTICK_BUTTON_UP) && e.jbutton.which == s_id) ||
                (e.type == SDL_EVENT_JOYSTICK_HAT_MOTION && e.jhat.which == s_id)))
            s_reported = true;
        if (e.type == SDL_EVENT_JOYSTICK_AXIS_MOTION && s_joystick && e.jaxis.which == s_id)
        {
            const int a = e.jaxis.axis;
            if (a >= 0 && size_t(a) < s_axisSeen.size() && !s_axisSeen[size_t(a)])
            {
                s_axisSeen[size_t(a)] = true;
                // SDL's first report of an axis carries where it was before it
                // moved: a calibration step measures such an axis from there.
                s_cal.FirstValue(a, e.jaxis.value);
                // The device is certainly reporting now: whether it can knock
                // is asked again if it said no (the centring: below).
                if (s_haptic && !s_canKnock && !s_canHapticRumble && SDL_InitHapticRumble(s_haptic))
                {
                    s_canHapticRumble = true;
                    fprintf(stderr, "[wheel] force feedback: knocks yes after all (the wheel was busy at first)\n");
                }
                // A pedal on this axis that had no resting place to work
                // from takes the value it was at as it started to move,
                // which is near enough where it rests. A wheel we know
                // never gets here: its model said so at the open.
                for (Step step : { Step::Throttle, Step::Brake, Step::Clutch })
                    if (AxisOf(step) == a && !s_pedals[Slot(step)].ready)
                    {
                        // Where it was as it started to move is near enough
                        // where it rests, and it can go as far as its axis.
                        const int16_t at = e.jaxis.value;
                        s_pedals[Slot(step)].Start(at, int16_t(at >= 0 ? -32768 : 32767));
                        fprintf(stderr, "[wheel] %s: axis %d first reported %+d, taken as where it rests\n", StepName(step),
                            a, int(at));
                    }
            }
        }
        else if (e.type == SDL_EVENT_JOYSTICK_ADDED)
        {
            Open();
        }
        else if (e.type == SDL_EVENT_JOYSTICK_REMOVED && s_joystick && e.jdevice.which == s_id)
        {
            fprintf(stderr, "[wheel] %s disconnected\n", s_name.c_str());
            StopCalibrate();
            Close();
            Open();  // a second wheel, if one is plugged in
        }
    }

    bool Found()
    {
        return s_joystick != nullptr;
    }

    bool Playing()
    {
        return InPlay();
    }

    bool Owns(SDL_JoystickID joystick)
    {
        return s_joystick != nullptr && joystick == s_id;
    }

    std::string Name()
    {
        return s_joystick ? s_name : std::string();
    }

    bool HasForceFeedback()
    {
        return s_haptic != nullptr && (s_canAutocentre || s_canSpring || s_canHapticRumble || s_canKnock);
    }

    bool Sample(Reading& out)
    {
        if (!s_joystick)
            return false;
        if (s_restAgain)
        {
            s_restAgain = false;
            Calibrate(true);  // a frame on, with the device certainly read
        }
        const std::array<bool, kMaxButtons> held = ButtonsNow();
        if (s_cal.Running())
            ApplyCalibration(s_cal.Update(AxisStates(false), s_axisSeen, held, HatNow(), JoystickButtonOf(wheelmap::button::A),
                JoystickButtonOf(wheelmap::button::B)));
        const wheelmap::Pressed pressed = wheelmap::Resolve(held, HatNow(), s_bindings);
        Reading r;
        r.buttons = pressed.buttons;
        r.rightY = pressed.rightY;
        r.back = (pressed.buttons & wheelmap::button::Back) != 0;
        r.start = (pressed.buttons & wheelmap::button::Start) != 0;
        if (s_axes.steer >= 0)
        {
            float turn = s_steering.Fraction(AxisNow(s_axes.steer));
            if (settings::GetBool(settings::Id::InvertSteering))
                turn = -turn;
            r.steer = wheelmap::Steer(turn, settings::GetInt(settings::Id::SteeringRange), s_fullTurn,
                settings::GetFloat(settings::Id::SteeringDeadzone), settings::GetFloat(settings::Id::SteeringResponse));
            r.stick = wheelmap::StickPastGameDeadzone(r.steer);
        }
        r.throttle = PedalValue(Step::Throttle);
        r.brake = PedalValue(Step::Brake);
        r.clutch = PedalValue(Step::Clutch);
        LogSample(held, r);
        s_last = r;
        // Switched off, the wheel plays nothing - but it was still read, so
        // Settings > Wheel keeps showing what it reads, and the menus (and
        // the chord that opens them) keep working from it.
        if (!settings::GetBool(settings::Id::Wheel))
            return false;
        out = r;
        return true;
    }

    Reading Last()
    {
        return s_joystick ? s_last : Reading{};
    }

    uint16_t HeldButtons()
    {
        if (!s_joystick || s_cal.Running())  // Calibrate has the wheel's buttons
            return 0;
        return wheelmap::Resolve(ButtonsNow(), HatNow(), s_bindings).buttons;
    }

    uint16_t ButtonFor(SDL_JoystickID joystick, int button)
    {
        if (!s_joystick || joystick != s_id || s_cal.Running())  // Calibrate has the wheel's buttons
            return 0;
        for (const Binding& b : s_bindings)
            if (b.index == button && b.role == wheelmap::Role::Button)
                return b.button;
        return 0;
    }

    void Rumble(uint16_t low, uint16_t high, bool vibration)
    {
        if (!s_joystick)
            return;
        const int percent = settings::GetInt(settings::Id::ForceFeedback);
        const float strength = vibration ? wheelmap::RumbleStrength(low, high, percent) : 0.0f;
        if (s_canKnock && !s_onlyOneEffect)
        {
            PlayKnock(strength);  // every frame: a knock is let go of over several
            return;
        }
        if (strength == s_rumble)
            return;
        s_rumble = strength;
        if (strength <= 0.0f)
        {
            StopRumble();
            return;
        }
        if (s_onlyOneEffect)
            return;  // the centring force has the wheel's one effect
        if (s_canHapticRumble && SDL_PlayHapticRumble(s_haptic, strength, 5000))
            return;
        if (s_canJoystickRumble)
            SDL_RumbleJoystick(s_joystick, uint16_t(low * percent / 100), uint16_t(high * percent / 100), 5000);
    }

    void Update()
    {
        if (!s_joystick)
            return;
        ApplyCentering();  // only when due: the setting changed, or the device's first report
        // How often the centring was said, once a minute while a wheel is open
        // (0 is the answer in play: a click a second was a re-send a second).
        const Uint64 now = SDL_GetTicks();
        if (s_haptic && now - s_centringLogAt >= 60000)
        {
            fprintf(stderr, "[wheel] centring said %d times in the last minute (%s at %d%%, %d since the wheel opened)\n",
                s_centring.sends - s_centringLogged, s_canAutocentre ? "autocentre" : s_canSpring ? "spring" : "none",
                settings::GetInt(settings::Id::CenteringForce), s_centring.sends);
            s_centringLogged = s_centring.sends;
            s_centringLogAt = now;
        }
    }

    void NoteMenu(bool open)
    {
        if (!s_joystick)
            return;
        const char* knocks = s_haptic && s_onlyOneEffect ? "none (the centring force has the wheel's one effect)"
            : s_haptic && s_canKnock                     ? "our sine"
            : s_haptic && s_canHapticRumble              ? "SDL's rumble"
            : s_canJoystickRumble                        ? "plain rumble"
                                                         : "none (no force feedback here)";
        fprintf(stderr, "[wheel] menu %s: knocks by %s (%d so far%s); centring %s at %d%%; Force Feedback %d%%; range %s, "
                        "response %.2f, deadzone %.2f\n",
            open ? "opened" : "closed", knocks, s_knocks, s_knock.playing ? ", one playing" : "",
            !s_haptic ? "none" : s_canAutocentre ? "autocentre" : s_canSpring ? "spring" : "none",
            settings::GetInt(settings::Id::CenteringForce),
            settings::GetInt(settings::Id::ForceFeedback),
            settings::FormatValue(settings::Id::SteeringRange, settings::GetValue(settings::Id::SteeringRange)).c_str(),
            double(settings::GetFloat(settings::Id::SteeringResponse)), double(settings::GetFloat(settings::Id::SteeringDeadzone)));
    }

    void Suspend()
    {
        StopRumble();
    }

    void Shutdown()
    {
        Close();
        s_started = false;
    }

    void StartCalibrate()
    {
        if (!s_joystick)
            return;
        s_cal.Start(AxisStates(false), s_axisSeen, ButtonsNow(), HatNow(), s_axes.steer);
        fprintf(stderr, "[wheel] calibrate: started\n");
    }

    bool Calibrating()
    {
        return s_cal.Running();
    }

    Step CalibratingStep()
    {
        return s_cal.Current();
    }

    Candidate Watch()
    {
        return s_cal.Watch();
    }

    bool KeepStep()
    {
        const auto event = s_cal.Keep(AxisStates(false));
        ApplyCalibration(event);
        return event == wheelmap::Calibration::Event::Kept;
    }

    void SkipStep()
    {
        ApplyCalibration(s_cal.Skip(AxisStates(false)));
    }

    void BackStep()
    {
        ApplyCalibration(s_cal.Back(AxisStates(false)));
    }

    void StopCalibrate()
    {
        ApplyCalibration(s_cal.Stop());
    }

    Assignment Axes()
    {
        return { s_axes.steer, s_axes.throttle, s_axes.brake, s_axes.clutch };
    }

    void ResetAxes()
    {
        StopCalibrate();
        for (settings::Id id : { settings::Id::WheelSteerAxis, settings::Id::WheelThrottleAxis, settings::Id::WheelBrakeAxis,
                                 settings::Id::WheelClutchAxis })
            settings::Set(id, -1);
        for (settings::Id id : { settings::Id::InvertSteering, settings::Id::ThrottleRest, settings::Id::ThrottleFull,
                                 settings::Id::BrakeRest, settings::Id::BrakeFull, settings::Id::ClutchRest,
                                 settings::Id::ClutchFull, settings::Id::SteerLeft, settings::Id::SteerRight })
            settings::Reset(id);
        for (size_t i = 0; i < wheelmap::kControlCount; i++)
            settings::Reset(ButtonOption(wheelmap::Control(i)));
        if (s_joystick)
            TakeBindings();
        if (s_joystick)
            Calibrate(false);
    }
}
