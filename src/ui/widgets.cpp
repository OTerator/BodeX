#include "ui/widgets.h"

#include "ui/BidiInput.h"  // bidiNoteInput (sub-question labels)
#include "model/Project.h" // gt::TextDir, Question, normalizeQuestion, equalShare
#include "model/Bidi.h"
#include "imgui_stdlib.h"  // ImGui::InputText(std::string&)

#include <cmath>
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

bool questionConfigBlock(gt::Question& q, int index)
{
    const ImVec4 kOk(0.35f, 0.85f, 0.40f, 1.0f);
    const ImVec4 kWarn(0.95f, 0.65f, 0.20f, 1.0f);
    bool changed = false;

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Q%d", index + 1);

    ImGui::SameLine(50);
    ImGui::SetNextItemWidth(150);
    if (ImGui::InputText("title##t", &q.title)) changed = true;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    if (ImGui::InputDouble("points##m", &q.maxPoints, 0.0, 0.0, "%.2f")) {
        if (q.maxPoints < 0) q.maxPoints = 0;
        gt::normalizeQuestion(q);
        changed = true;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("sub-Qs##s", &q.subCount)) {
        if (q.subCount < 1)  q.subCount = 1;
        if (q.subCount > 50) q.subCount = 50;
        gt::normalizeQuestion(q);
        changed = true;
    }

    ImGui::SameLine();
    int splitInt = (q.split == gt::SplitMode::Custom) ? 1 : 0;
    if (ImGui::RadioButton("Equal", &splitInt, 0)) { q.split = gt::SplitMode::Equal;  gt::normalizeQuestion(q); changed = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("Custom", &splitInt, 1)) { q.split = gt::SplitMode::Custom; gt::normalizeQuestion(q); changed = true; }

    if (q.split == gt::SplitMode::Custom) {
        if (static_cast<int>(q.subPoints.size()) != q.subCount)
            q.subPoints.resize(static_cast<size_t>(q.subCount), gt::equalShare(q));
        ImGui::Indent(50);
        double sum = 0.0;
        for (int k = 0; k < q.subCount; ++k) {
            ImGui::PushID(k);
            ImGui::SetNextItemWidth(70);
            if (ImGui::InputDouble("##sp", &q.subPoints[static_cast<size_t>(k)], 0.0, 0.0, "%.2f")) changed = true;
            sum += q.subPoints[static_cast<size_t>(k)];
            ImGui::PopID();
            if ((k % 6) != 5 && k != q.subCount - 1)
                ImGui::SameLine();
        }
        const bool matches = std::fabs(sum - q.maxPoints) < 1e-6;
        ImGui::TextColored(matches ? kOk : kWarn, "sub-points sum = %s / %s",
                           fmtNum(sum).c_str(), fmtNum(q.maxPoints).c_str());
        ImGui::Unindent(50);
    } else {
        ImGui::Indent(50);
        ImGui::TextDisabled("each of %d sub-questions = %s pts", q.subCount, fmtNum(gt::equalShare(q)).c_str());
        ImGui::Unindent(50);
    }

    if (q.subCount >= 2) {
        if (static_cast<int>(q.subLabels.size()) != q.subCount)
            q.subLabels.resize(static_cast<size_t>(q.subCount));
        ImGui::Indent(50);
        ImGui::TextDisabled("sub-question labels (optional):");
        for (int k = 0; k < q.subCount; ++k) {
            ImGui::PushID(k);
            gt::TextDir tmp = gt::TextDir::Auto; // labels persist no direction of their own
            if (bidiNoteInput("##lbl", q.subLabels[static_cast<size_t>(k)], tmp, 110.0f, true)) changed = true;
            ImGui::PopID();
            if ((k % 6) != 5 && k != q.subCount - 1)
                ImGui::SameLine();
        }
        ImGui::Unindent(50);
    }

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

std::vector<std::string> cellNoteLinesVisual(const gt::Question& q, const gt::Cell& c)
{
    std::vector<std::string> lines;
    if (!c.note.empty())
        lines.push_back(noteVisual(c.note, c.noteDir));
    for (const auto& sn : c.subNotes)
        lines.push_back(noteVisual(gt::subHeader(q, sn.sub) + ": " + sn.text, sn.dir));
    return lines;
}

} // namespace gt::ui
