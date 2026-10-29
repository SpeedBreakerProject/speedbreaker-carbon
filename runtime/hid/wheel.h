// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// A USB steering wheel (the Logitech G29 and its family, and any other
// wheel SDL sees) as the Xbox 360 controller the game reads: the device
// itself, its force feedback, and what it amounts to each frame.
// hid/wheel_map.h holds the rules this follows; hid/input.cpp puts a
// Reading in the pad, ui/ui.cpp asks ButtonFor for the settings chord and
// the menu's navigation, and ui/settings_menu.cpp draws Settings > Wheel.
//
//   - A wheel is not a gamepad: SDL has no gamepad mapping for one, so it
//     arrives as a plain joystick. The first one connected is used, and a
//     wheel is preferred to a controller while both are plugged in. One
//     connecting or going mid-game is picked up (Initialize is not needed
//     again).
//   - Force feedback goes through SDL's haptics. On a Mac that is SDL 3.4's
//     own Logitech driver (src/haptic/hidapi/SDL_hidapihaptic_lg4ff.c),
//     which talks to the wheel over HID with nothing of Logitech's
//     installed: the wheels its joystick driver takes (a G29 in its PS3
//     position, 046d:c24f; the G27, G25, Driving Force GT and Pro), not a
//     G29 in its PS4 position, a G920 or a G923. On Linux it is the
//     kernel's driver (untested here: mainline hid-lg4ff offers a constant
//     force and autocentre, not the sine SDL's rumble needs). Where a
//     system can't drive a wheel, the log says so and Settings > Wheel says
//     so on the two rows that need the motors, while steering, the pedals
//     and the buttons all work regardless.
#pragma once
#include <cstdint>
#include <string>

#include <SDL3/SDL_events.h>

#include "wheel_map.h"

namespace hid::wheel
{
    // Main thread, after SDL's gamepad (and so joystick) subsystem is up.
    // Not called at all while NFSMW_VIRTUAL_PAD's test controller is
    // attached: that run has its own idea of what is plugged in.
    void Initialize();
    // Main thread: every SDL event, for a wheel coming or going.
    void HandleEvent(const SDL_Event& event);
    // Main thread: a wheel is plugged in and open (Settings > Wheel's page
    // shows itself while one is).
    bool Found();
    // Main thread: ...and Settings > Wheel has it on, so the game plays
    // from it (a controller is then left alone). With it off the wheel
    // still works the menus, so the page that switched it off can switch it
    // back on.
    bool Playing();
    // Main thread: this joystick is the open wheel, so the gamepad path
    // must leave it be.
    bool Owns(SDL_JoystickID joystick);
    // Main thread: the open wheel's name, "" with none.
    std::string Name();
    // Main thread: the wheel's force feedback can be driven on this system
    // (Settings > Wheel says so where it can't).
    bool HasForceFeedback();

    // The wheel as the pad, before the deadzones and the keyboard
    // (hid/input.cpp): the buttons as XInput's bits, with Back among them,
    // and the axes as fractions, Settings > Wheel already applied.
    struct Reading
    {
        uint16_t buttons = 0;
        float steer = 0.0f;     // -1 (full left) to 1: how far the wheel steers (Settings > Wheel applied)
        float stick = 0.0f;     // the left stick's X that steers the car that far: steer past the
                                // game's own deadzone (wheel_map.h's StickPastGameDeadzone)
        float throttle = 0.0f;  // 0 to 1: the right trigger
        float brake = 0.0f;     // 0 to 1: the left trigger
        float clutch = 0.0f;    // 0 to 1: nothing on the pad; Settings > Wheel shows it
        int16_t rightY = 0;     // the paddles as the right stick, which is how the game shifts
        bool back = false, start = false;  // as the device reports them (the settings chord)
    };
    // Main thread, once a frame. False, with `out` left alone, when no
    // wheel is in play; the device is still read, so the page's readings
    // stay live with the wheel switched off.
    bool Sample(Reading& out);
    // Main thread: what the last Sample read, for Settings > Wheel to show
    // (the page is drawn between two samples). All zero with no wheel.
    Reading Last();

    // Main thread: the pad buttons the wheel is holding down now (ui/ui.cpp
    // feeds them to ImGui, which knows only gamepads). 0 with no wheel.
    uint16_t HeldButtons();
    // Main thread: which pad button this joystick's button is, for the Back
    // + Start chord (ui/ui.cpp). 0: none, or another device's.
    uint16_t ButtonFor(SDL_JoystickID joystick, int button);

    // Main thread, once a frame: the rumble the game asks for, as Settings >
    // Wheel > Force Feedback and Settings > Input > Vibration allow. Where
    // the wheel plays a sine (the Logitech wheels on a Mac) it is a knock of
    // its own (wheel_map.h's Knock), let go of over a few frames; elsewhere
    // SDL's rumble, told only when the request changes.
    void Rumble(uint16_t low, uint16_t high, bool vibration);
    // Main thread, once a frame: keeps the wheel's centring force as
    // Settings > Wheel asks.
    void Update();
    // Main thread: a menu over the game opened or closed (the game sees an
    // idle pad meanwhile). Logs what the wheel's force feedback is doing and
    // the Wheel settings, so a session's log says it at every return.
    void NoteMenu(bool open);
    // Main thread: the game is suspended, or stopping. Suspend stops what
    // the wheel is playing without letting the device go.
    void Suspend();
    void Shutdown();

    // Settings > Wheel > Calibrate (ui/settings_menu.cpp draws it): the wheel's
    // two ends, each pedal, then a button per pad control, one step at a
    // time. hid/wheel_map.h's Calibration holds the rules (and says what each
    // step watches for); this side feeds it the device every frame (Sample)
    // and stores each step as it is kept, so stopping keeps what is done.
    //
    // While it runs the wheel's buttons are its own: they don't reach the
    // menus (HeldButtons is 0) or the Back + Start chord (ButtonFor is 0), so
    // Circle can be B and Share Back. The wheel's A keeps an axis step, its B
    // stops one, and its D-pad moves along: right skips, left goes back, down
    // stops. A button step takes the button pressed, kept through its release,
    // and A or that button again keeps it.
    using Step = wheelmap::Step;
    constexpr size_t kStepCount = wheelmap::kStepCount;
    // Main thread: what the step asks the player to do ("Turn the wheel
    // fully left", "Press the throttle to the floor"), and a short name for
    // it ("Full left", "Throttle", "A").
    const char* StepAsk(Step step);
    const char* StepName(Step step);
    // Is it a button the step is after, rather than an axis?
    bool StepIsButton(Step step);
    // Main thread: start at the first step (a wheel must be found).
    void StartCalibrate();
    bool Calibrating();
    // Main thread: which step is running (only meaningful while Calibrating).
    Step CalibratingStep();
    using Candidate = wheelmap::Calibration::Found;
    // Main thread, while a step runs: what keeping it would store.
    Candidate Watch();
    // Main thread: the keyboard's or a controller's A, right, left, Esc / B
    // (the wheel's own come through Sample). Keep is false while the step has
    // nothing yet, and the step carries on.
    bool KeepStep();
    void SkipStep();
    void BackStep();
    void StopCalibrate();
    // Main thread: the axis each role is reading, as the device numbers
    // them; -1: none (shown in Settings > Wheel's Calibrate row).
    struct Assignment { int steer = -1, throttle = -1, brake = -1, clutch = -1; };
    Assignment Axes();
    // Main thread: forget what Calibrate stored (the axes and the steering
    // direction) and work them out from the device again (the page's Reset).
    void ResetAxes();
}
