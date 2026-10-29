// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// NFS: Carbon has no Smooth Motion (game/hfr.h): Most Wanted's
// (game/hfr.cpp) hooks Most Wanted's frame, render entry and world stepper,
// none of which carries over (Carbon's stepper counts seconds:
// game/carbon/frame_time.cpp). Off for every run, so the engine's callers
// (the renderer's Auto, the command processor's frame costs, the
// presenter's grid, the vblank's picture target) take their 60-pictures
// path, and Settings > Display doesn't show the row (user/settings.cpp).
#include <stdafx.h>
#include <game/hfr.h>

namespace hfr
{
    void Init() {}
    bool Enabled() { return false; }
    bool Render2() { return false; }
    bool InBetween() { return false; }
    float InBetweenFraction() { return 0.0f; }
    float OwnFraction() { return 0.0f; }
    int64_t PictureTarget() { return 0; }
    void NoteSwap(float) {}
    void NoteFrameCost(double, double, bool) {}
    void NotePictureCost(bool, double, uint64_t, uint64_t) {}
    // The presenter's late-picture and hold-change notes (off Apple's
    // systems, video/presenter.cpp): nothing to drop to 60 from. Every
    // function hfr.h declares is defined here (carbon_hfr_stub_test), or a
    // Linux build doesn't link.
    void NoteLatePicture() {}
    void NoteHoldChange() {}
}
