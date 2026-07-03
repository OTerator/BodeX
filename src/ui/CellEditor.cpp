#include "ui/CellEditor.h"

#include "app/App.h"
#include "ui/widgets.h"
#include "model/Scoring.h"
#include "imgui.h"
#include "imgui_stdlib.h"

namespace gt::ui {

namespace {
const ImVec4 kWarn(0.95f, 0.65f, 0.20f, 1.0f);

// Extract a page number from the stored (free-text) last-page string.
// "14" -> 14, "p.14" -> 14, "" -> 0. 0 means "no last page".
int lastPageToInt(const std::string& s)
{
    long v = 0;
    bool any = false;
    for (char ch : s) {
        if (ch >= '0' && ch <= '9') {
            v = v * 10 + (ch - '0');
            any = true;
            if (v > 100000) break;
        }
    }
    return any ? static_cast<int>(v) : 0;
}
} // namespace

void cellEditorPopup(App& app)
{
    if (!ImGui::BeginPopup("CellEditor"))
        return;

    const int si = app.editorStudent;
    const int qi = app.editorQuestion;
    if (si < 0 || qi < 0 ||
        si >= static_cast<int>(app.project.students.size()) ||
        qi >= static_cast<int>(app.project.questions.size())) {
        ImGui::EndPopup();
        return;
    }

    gt::Student&  s = app.project.students[static_cast<size_t>(si)];
    gt::Question& q = app.project.questions[static_cast<size_t>(qi)];
    gt::Cell&     c = s.cells[static_cast<size_t>(qi)];

    ImGui::Text("Student %d  -  %s", s.id, q.title.c_str());
    ImGui::TextDisabled("max %s pts  -  %d sub-questions (%s split)",
                        fmtNum(q.maxPoints).c_str(), q.subCount,
                        q.split == gt::SplitMode::Custom ? "custom" : "equal");
    ImGui::Separator();

    if (s.noSubmission)
        ImGui::TextColored(kWarn, "Student marked NO SUBMISSION - the whole row scores 0.");

    // Green full-marks tick: overrides awarded points to the question maximum.
    bool tick = c.fullTick;
    if (greenTickCheckbox("Full marks - all sub-questions correct", &tick)) {
        c.fullTick = tick;
        c.touched = true;
        app.markDirty();
    }

    // Manually awarded points (disabled while full-tick is on).
    ImGui::BeginDisabled(c.fullTick);
    double awarded = c.awarded;
    ImGui::SetNextItemWidth(140);
    if (ImGui::InputDouble("Awarded points", &awarded, 0.5, 1.0, "%.2f")) {
        c.awarded = awarded;
        c.touched = true;
        app.markDirty();
    }
    ImGui::EndDisabled();
    if (gt::cellOverMax(q, c))
        ImGui::TextColored(kWarn, "awarded exceeds the max of %s", fmtNum(q.maxPoints).c_str());

    // Sub-questions answered (X of Y) - reference only, does not affect score.
    int answered = c.subAnswered;
    ImGui::SetNextItemWidth(140);
    if (ImGui::InputInt("Sub-questions answered", &answered)) {
        if (answered < 0)          answered = 0;
        if (answered > q.subCount) answered = q.subCount;
        c.subAnswered = answered;
        c.touched = true;
        app.markDirty();
    }
    ImGui::SameLine();
    ImGui::Text("/ %d", q.subCount);

    // Resume marker: page number with -/+ steppers (type or step; 0 clears it).
    int lp = lastPageToInt(c.lastPage);
    ImGui::SetNextItemWidth(140);
    if (ImGui::InputInt("Last page (lp)", &lp)) {
        if (lp < 0) lp = 0;
        c.lastPage = (lp > 0) ? std::to_string(lp) : std::string();
        c.touched = true;
        app.markDirty();
    }
    if (ImGui::InputTextMultiline("Note", &c.note, ImVec2(280, 56))) { c.touched = true; app.markDirty(); }

    ImGui::Separator();
    ImGui::Text("Cell score: %s / %s",
                fmtNum(gt::cellPoints(s, q, c)).c_str(), fmtNum(q.maxPoints).c_str());
    ImGui::Separator();

    if (ImGui::Button("Clear cell")) {
        c = gt::Cell{};
        app.markDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void studentMenuPopup(App& app)
{
    if (!ImGui::BeginPopup("StudentMenu"))
        return;

    const int si = app.menuStudent;
    if (si < 0 || si >= static_cast<int>(app.project.students.size())) {
        ImGui::EndPopup();
        return;
    }

    gt::Student& s = app.project.students[static_cast<size_t>(si)];
    ImGui::Text("Student %d", s.id);
    ImGui::Separator();

    bool ns = s.noSubmission;
    if (ImGui::Checkbox("No submission (score 0)", &ns)) {
        s.noSubmission = ns;
        app.markDirty();
    }

    ImGui::TextDisabled("Total: %s / %s",
                        fmtNum(gt::studentTotal(app.project, static_cast<size_t>(si))).c_str(),
                        fmtNum(gt::projectMaxTotal(app.project)).c_str());

    ImGui::Separator();
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

} // namespace gt::ui
