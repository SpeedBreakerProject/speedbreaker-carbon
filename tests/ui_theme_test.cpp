// Tests for ui::theme (runtime/ui/theme.h): each game's accent, and the
// contrast its strokes and fills keep on the menu's panel. Built twice by
// ctest: ui_theme_test (Most Wanted) and ui_theme_carbon_test
// (-DSB_GAME_CARBON). From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/ui_theme_test.cpp -o build/ui_theme_test
//   clang++ -std=c++20 -O2 -Iruntime -DSB_GAME_CARBON tests/ui_theme_test.cpp -o build/ui_theme_carbon_test
// Given ui/ui.cpp's path (ctest passes it), it checks the style takes its
// accent from here and nowhere else.
#include <ui/theme.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace
{
    using ui::theme::Rgb;

    // WCAG 2 relative luminance and contrast ratio (sRGB).
    double Linear(double c)
    {
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    }

    double Luminance(Rgb c)
    {
        return 0.2126 * Linear(c.r) + 0.7152 * Linear(c.g) + 0.0722 * Linear(c.b);
    }

    double Contrast(Rgb a, Rgb b)
    {
        const double la = Luminance(a), lb = Luminance(b);
        return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
    }

    Rgb Over(Rgb top, float alpha, Rgb under)
    {
        return { top.r * alpha + under.r * (1 - alpha), top.g * alpha + under.g * (1 - alpha), top.b * alpha + under.b * (1 - alpha) };
    }

    // ui.cpp's ApplyStyle: the panel and the text.
    constexpr Rgb kPanel = { 0.055f, 0.065f, 0.085f };
    constexpr Rgb kText = { 0.93f, 0.94f, 0.96f };
    // The fills' alphas there: rows/tabs/hovered buttons, and a pressed button.
    constexpr float kRowFills[] = { 0.15f, 0.16f, 0.22f, 0.25f, 0.30f, 0.32f, 0.35f };
    constexpr float kPressed = 0.55f;

    bool Near(float a, float b)
    {
        return std::abs(a - b) < 1e-6f;
    }

    void Colours()
    {
#ifdef SB_GAME_CARBON
        printf("Carbon's teal\n");
        CHECK(Near(ui::theme::kAccent.r, 0x48 / 255.0f) && Near(ui::theme::kAccent.g, 0xE0 / 255.0f) && Near(ui::theme::kAccent.b, 0xE0 / 255.0f),
            "strokes: #48E0E0 (carbon/DECISIONS.md: the HUD's gauges)");
        CHECK(Near(ui::theme::kAccentFill.r, 0x80 / 255.0f) && Near(ui::theme::kAccentFill.g, 0xD0 / 255.0f) && Near(ui::theme::kAccentFill.b, 0xE0 / 255.0f),
            "fills: #80D0E0 (the menu frame)");
        // Not amber: blue and green over red, by a wide margin.
        CHECK(ui::theme::kAccent.b > ui::theme::kAccent.r + 0.5f && ui::theme::kAccentFill.b > ui::theme::kAccentFill.r + 0.3f, "teal, not amber");
#else
        printf("Most Wanted's amber\n");
        // The literal Most Wanted always had: its menus unchanged bit for bit.
        CHECK(ui::theme::kAccent.r == 1.00f && ui::theme::kAccent.g == 0.66f && ui::theme::kAccent.b == 0.12f, "amber 1.00 0.66 0.12");
        CHECK(ui::theme::kAccentFill.r == ui::theme::kAccent.r && ui::theme::kAccentFill.g == ui::theme::kAccent.g &&
                  ui::theme::kAccentFill.b == ui::theme::kAccent.b,
            "one amber for strokes and fills");
        CHECK(std::abs(ui::theme::kAccent.g - 0xA8 / 255.0f) < 0.002f && std::abs(ui::theme::kAccent.b - 0x1F / 255.0f) < 0.002f, "#FFA81F");
#endif
        CHECK(ui::theme::FromHex(0x48E0E0).g == 0xE0 / 255.0f && ui::theme::FromHex(0x0000FF).b == 1.0f && ui::theme::FromHex(0xFF0000).g == 0.0f,
            "FromHex");
    }

    void ContrastRules()
    {
        printf("contrast\n");
        const Rgb amber = { 1.00f, 0.66f, 0.12f };
        const double strokes = Contrast(ui::theme::kAccent, kPanel);
        CHECK(strokes >= 7.0, "strokes on the panel %.2f:1", strokes);
        CHECK(strokes >= Contrast(amber, kPanel) - 0.01, "strokes %.2f:1, below the amber's %.2f:1", strokes, Contrast(amber, kPanel));
        for (float a : kRowFills)
        {
            const double t = Contrast(kText, Over(ui::theme::kAccentFill, a, kPanel));
            CHECK(t >= 7.0, "text over a %.2f fill %.2f:1", a, t);
        }
        const double pressed = Contrast(kText, Over(ui::theme::kAccentFill, kPressed, kPanel));
        CHECK(pressed >= 4.0, "text over a pressed button %.2f:1", pressed);
        // The strokes still read over the selection's own fill.
        const double cursor = Contrast(ui::theme::kAccent, Over(ui::theme::kAccentFill, 0.22f, kPanel));
        CHECK(cursor >= 4.5, "the cursor's stroke over a selected row %.2f:1", cursor);
    }

    void Source(const char* path)
    {
        printf("ui.cpp\n");
        std::ifstream f(path);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string src = ss.str();
        CHECK(!src.empty(), "can't read %s", path);
        CHECK(src.find("#include \"theme.h\"") != std::string::npos, "ui.cpp includes theme.h");
        CHECK(src.find("const ImVec4 kAccent(theme::kAccent.r, theme::kAccent.g, theme::kAccent.b, 1.00f);") != std::string::npos,
            "the accent from the theme");
        CHECK(src.find("const ImVec4 kAccentFill(theme::kAccentFill.r, theme::kAccentFill.g, theme::kAccentFill.b, 1.00f);") != std::string::npos,
            "the fill from the theme");
        CHECK(src.find("1.00f, 0.66f, 0.12f") == std::string::npos, "an amber literal left in ui.cpp");
        for (const char* row : { "c[ImGuiCol_Header] = fill(", "c[ImGuiCol_ButtonHovered] = fill(", "c[ImGuiCol_TabSelected] = fill(",
                 "c[ImGuiCol_CheckMark] = kAccent;", "c[ImGuiCol_NavCursor] = kAccent;", "c[ImGuiCol_SliderGrabActive] = kAccent;" })
            CHECK(src.find(row) != std::string::npos, "missing: %s", row);
    }
}

int main(int argc, char** argv)
{
    Colours();
    ContrastRules();
    if (argc > 1)
        Source(argv[1]);
    if (g_failures)
    {
        printf("%d FAILURES\n", g_failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
