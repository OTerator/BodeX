#include "ui/CellEditor.h"

#include "app/App.h"
#include "ui/widgets.h"
#include "model/Scoring.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include <cstdio>

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

    // Green full-marks tick: full points and, by definition, every sub answered.
    bool tick = c.fullTick;
    if (greenTickCheckbox("Full marks - all sub-questions correct", &tick)) {
        c.fullTick = tick;
        if (tick) {
            c.subAnswered = q.subCount;
            if (q.split == gt::SplitMode::Custom)
                c.subChecks.assign(static_cast<size_t>(q.subCount), 1);
        }
        c.touched = true;
        app.markDirty();
    }

    // Awardable ceiling after locking out skipped sub-questions (== maxPoints when
    // all answered). Manually awarded points are capped at it.
    const double effMax = gt::effectiveMax(q, c);

    // Awarded points: -/+ step buttons dock from full (first '-' on a blank or full
    // cell assumes full marks, later presses deduct — see gt::stepAwarded), plus a
    // field for an exact value. The buttons act even on a full tick (that is the
    // "assume full then dock" path), so they sit outside the disabled block.
    if (ImGui::Button("-##award")) { gt::stepAwarded(q, c, -1.0); app.markDirty(); }
    ImGui::SameLine();
    if (ImGui::Button("+##award")) { gt::stepAwarded(q, c, +1.0); app.markDirty(); }
    ImGui::SameLine();
    ImGui::BeginDisabled(c.fullTick);
    double awarded = c.awarded;
    ImGui::SetNextItemWidth(140);
    char awardedLabel[48];
    std::snprintf(awardedLabel, sizeof(awardedLabel), "Awarded points (max %s)", fmtNum(effMax).c_str());
    if (ImGui::InputDouble(awardedLabel, &awarded, 0.0, 0.0, "%.2f")) {  // step 0 -> no built-in buttons
        if (awarded < 0.0)    awarded = 0.0;
        if (awarded > effMax) awarded = effMax;
        c.awarded = awarded;
        c.touched = true;
        app.markDirty();
    }
    ImGui::EndDisabled();
    if (gt::cellOverMax(q, c))
        ImGui::TextColored(kWarn, "awarded exceeds the awardable max of %s", fmtNum(effMax).c_str());

    // Sub-questions answered. Skipping one locks out its points: the value is
    // deducted from `awarded` and from the awardable ceiling. Equal split edits a
    // count; Custom ticks each sub-question so it deducts its own point value.
    if (q.split == gt::SplitMode::Custom) {
        ImGui::TextUnformatted("Sub-questions answered (untick a skipped one):");
        if (static_cast<int>(c.subChecks.size()) != q.subCount)
            c.subChecks.assign(static_cast<size_t>(q.subCount), 1);
        for (int k = 0; k < q.subCount; ++k) {
            ImGui::PushID(k);
            bool ans = c.subChecks[static_cast<size_t>(k)] != 0;
            const double kpts = k < static_cast<int>(q.subPoints.size())
                                ? q.subPoints[static_cast<size_t>(k)] : 0.0;
            char lbl[32];
            std::snprintf(lbl, sizeof(lbl), "%d (%s)", k + 1, fmtNum(kpts).c_str());
            if (ImGui::Checkbox(lbl, &ans)) {
                gt::setSubAnswered(q, c, k, ans); // deduct/add this sub-q's points
                app.markDirty();
            }
            ImGui::PopID();
            if ((k % 6) != 5 && k != q.subCount - 1)
                ImGui::SameLine();
        }
    } else {
        int answered = c.subAnswered;
        ImGui::SetNextItemWidth(140);
        if (ImGui::InputInt("Sub-questions answered", &answered)) {
            gt::setAnsweredCount(q, c, answered); // deducts skipped shares from awarded
            app.markDirty();
        }
        ImGui::SameLine();
        ImGui::Text("/ %d", q.subCount);
    }

    // Resume marker: page number with -/+ steppers (type or step; 0 clears it).
    int lp = lastPageToInt(c.lastPage);
    ImGui::SetNextItemWidth(140);
    if (ImGui::InputInt("Last page (lp)", &lp)) {
        if (lp < 0) lp = 0;
        c.lastPage = (lp > 0) ? std::to_string(lp) : std::string();
        c.touched = true;
        app.markDirty();
    }
    // Note (Hebrew / RTL aware). Editing runs in logical order — ImGui's InputText
    // has no RTL mode — under the Hebrew-capable notes font so glyphs render. The
    // Ctrl+Left-Shift / Ctrl+Right-Shift toggle sets the base direction (Windows
    // convention) while the field is focused, and the live preview below shows how
    // the note will read (visual order, right-aligned when RTL).
    pushNotesFont();
    const bool noteChanged = ImGui::InputTextMultiline("Note", &c.note, ImVec2(280, 56));
    popNotesFont();
    const bool noteActive = ImGui::IsItemActive();
    if (noteChanged) { c.touched = true; app.markDirty(); }

    if (noteActive && ImGui::GetIO().KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftShift, false))  { c.noteDir = gt::TextDir::LTR; app.markDirty(); }
        if (ImGui::IsKeyPressed(ImGuiKey_RightShift, false)) { c.noteDir = gt::TextDir::RTL; app.markDirty(); }
    }

    const char* dirLabel = (c.noteDir == gt::TextDir::LTR) ? "LTR"
                         : (c.noteDir == gt::TextDir::RTL) ? "RTL" : "auto";
    ImGui::TextDisabled("note dir: %s   (Ctrl+Shift:  L = left-to-right,  R = right-to-left)", dirLabel);
    if (!c.note.empty()) {
        const std::string vis = gt::ui::noteVisual(c.note, c.noteDir);
        const bool rtl = gt::ui::noteIsRtl(c.note, c.noteDir);
        pushNotesFont();
        if (rtl) {
            const float avail = ImGui::GetContentRegionAvail().x;
            const float tw = ImGui::CalcTextSize(vis.c_str()).x;
            if (tw < avail) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw));
        }
        ImGui::TextUnformatted(vis.c_str());
        popNotesFont();
    }

    ImGui::Separator();
    ImGui::Text("Cell score: %s / %s",
                fmtNum(gt::cellPoints(s, q, c)).c_str(), fmtNum(gt::effectiveMax(q, c)).c_str());
    const double locked = gt::lockedSubPoints(q, c);
    if (locked > 1e-9)
        ImGui::TextDisabled("locked out %s of %s (skipped sub-questions)",
                            fmtNum(locked).c_str(), fmtNum(q.maxPoints).c_str());
    ImGui::Separator();

    if (ImGui::Button("Clear cell")) {
        c = gt::blankCell(q);   // back to all-answered, not a raw zeroed cell
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
