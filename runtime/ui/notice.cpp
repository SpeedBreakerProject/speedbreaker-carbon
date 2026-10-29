// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See ui.h.
//
// A one-time notice panel over the game (the refresh-rate one,
// video/refresh_notice.h): a title, a paragraph and OK, drawn like the
// "What's new" note. The game pauses while it shows, as for the settings
// menu (the 360's Guide), and goes on when it closes.
#include <stdafx.h>
#include "ui.h"

#include <kernel/xam.h>

#include <imgui.h>

#include <memory>

namespace ui
{
    namespace
    {
        struct Notice
        {
            std::string title, text;
            std::function<void(const char*)> closed;
            bool firstFrame = true;
        };

        void Close(Notice& notice, const char* how)
        {
            fprintf(stderr, "[ui] notice \"%s\" closed (%s)\n", notice.title.c_str(), how);
            auto closed = std::move(notice.closed);
            SetModal(nullptr);  // destroys `notice`
            XamNotifySystemUi(false);
            if (closed)
                closed(how);
        }

        void Draw(Notice& notice)
        {
            float s = Scale();
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            float width = std::min(viewport->WorkSize.x - 2 * kSpaceL * s, 640.0f * s);
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, viewport->WorkSize.y));
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_AlwaysAutoResize;
            if (notice.firstFrame || !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetNextWindowFocus();
            if (!ImGui::Begin("Notice", nullptr, flags))
            {
                ImGui::End();
                return;
            }
            TitleText(notice.title.c_str());
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextWrapped("%s", notice.text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            if (notice.firstFrame)
                ImGui::SetKeyboardFocusHere();
            bool ok = ImGui::Button("OK");
            ImGui::SameLine();
            {
                const char* legend = "A or B: close";
                float w = ImGui::CalcTextSize(legend).x;
                ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - w));
                ImGui::TextDisabled("%s", legend);
            }
            notice.firstFrame = false;
            const char* how = ok ? "OK" : nullptr;
            if (!how && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                how = "B or Esc";
            else if (!how && ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false))
                how = "Start";
            ImGui::End();
            if (how)
                Close(notice, how);
        }
    }

    bool ShowNotice(std::string title, std::string text, std::function<void(const char*)> closed)
    {
        if (!Ready() || HasModal() || SettingsOpen())
            return false;
        auto notice = std::make_shared<Notice>();
        notice->title = std::move(title);
        notice->text = std::move(text);
        notice->closed = std::move(closed);
        fprintf(stderr, "[ui] notice \"%s\": %s\n", notice->title.c_str(), notice->text.c_str());
        SetModal([notice] { Draw(*notice); });
        // As the settings menu: the game pauses under it.
        XamNotifySystemUi(true);
        return true;
    }
}
