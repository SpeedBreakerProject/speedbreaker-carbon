// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The UI's accent colour, per game (SB_GAME, top-level CMakeLists.txt): the
// one place the settings menu, the toasts, the notices, the installer and
// every other screen of ours take it from (ui/ui.cpp's ApplyStyle and
// PushAccentText). Header only.
//
//   - Most Wanted: amber, #FFA81F (the website's --sb-amber): strokes and
//     fills alike.
//   - Carbon: the game's own teal (sampled from its screens): #48E0E0, its
//     HUD gauges', for the strokes (check marks, slider grabs, the
//     selection's cursor, the tab's overline, titles), and #80D0E0, its menu
//     frame's, for the translucent fills behind a hovered or selected row, a
//     button and a tab.
//
// Contrast, the teal against the amber (tests/ui_theme_test.cpp checks it;
// WCAG ratios on the menu's panel colour, 0.055 0.065 0.085): the strokes
// against the panel 11.7:1 (amber 9.8:1); the menu's text over the fills it
// sits on (rows, tabs, hovered buttons: at most 0.35 of the fill colour over
// the panel) 7.1:1 or more (amber 7.6:1), over a button being pressed (0.55)
// 4.1:1 (amber 4.5:1).
#pragma once

namespace ui::theme
{
    struct Rgb
    {
        float r, g, b;
    };

    // From 0xRRGGBB.
    constexpr Rgb FromHex(unsigned rgb)
    {
        return { float((rgb >> 16) & 0xFF) / 255.0f, float((rgb >> 8) & 0xFF) / 255.0f, float(rgb & 0xFF) / 255.0f };
    }

#ifdef SB_GAME_CARBON
    constexpr Rgb kAccent = FromHex(0x48E0E0);      // strokes: Carbon's HUD teal
    constexpr Rgb kAccentFill = FromHex(0x80D0E0);  // fills: Carbon's menu frame
#else
    // Most Wanted's literal as it always was (#FFA81F to within a level).
    constexpr Rgb kAccent = { 1.00f, 0.66f, 0.12f };
    constexpr Rgb kAccentFill = kAccent;
#endif
}
