#include "ui/widgets.h"

#include <cstdio>

namespace gt::ui {

std::string fmtNum(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0')
            s.pop_back();
        if (!s.empty() && s.back() == '.')
            s.pop_back();
    }
    return s;
}

bool greenTickCheckbox(const char* label, bool* v)
{
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.30f, 0.90f, 0.35f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,   ImVec4(0.16f, 0.34f, 0.18f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.44f, 0.24f, 1.00f));
    bool changed = ImGui::Checkbox(label, v);
    ImGui::PopStyleColor(3);
    return changed;
}

void bigTitle(const char* text, float px)
{
    ImGui::PushFont(nullptr, px); // keep current font family, larger size
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

} // namespace gt::ui
