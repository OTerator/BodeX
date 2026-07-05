#include "ui/widgets.h"

#include "model/Project.h" // gt::TextDir
#include "model/Bidi.h"

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

// The Hebrew-capable font for cell notes (nullptr until main.cpp loads it, and if
// the font file is missing it stays nullptr — notes then use the default font).
static ImFont* g_notesFont = nullptr;

void    setNotesFont(ImFont* f) { g_notesFont = f; }
ImFont* notesFont()             { return g_notesFont; }

// PushFont(font, 0.0f) swaps the family but keeps the current size; a null font
// keeps the current family too, so this is always safe and always balanced by pop.
void pushNotesFont() { ImGui::PushFont(g_notesFont, 0.0f); }
void popNotesFont()  { ImGui::PopFont(); }

static gt::BaseDir baseDirOf(gt::TextDir d)
{
    switch (d) {
        case gt::TextDir::LTR: return gt::BaseDir::LTR;
        case gt::TextDir::RTL: return gt::BaseDir::RTL;
        default:               return gt::BaseDir::Auto;
    }
}

std::string noteVisual(const std::string& logicalUtf8, gt::TextDir dir)
{
    return gt::bidiVisualUtf8(logicalUtf8, baseDirOf(dir));
}

bool noteIsRtl(const std::string& logicalUtf8, gt::TextDir dir)
{
    return gt::bidiBaseIsRtl(logicalUtf8, baseDirOf(dir));
}

} // namespace gt::ui
