#include "ui/CellEditor.h"

#include "app/App.h"
#include "ui/widgets.h"
#include "ui/BidiInput.h"
#include "model/Scoring.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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

    // The editor was re-clicked onto a different cell while still open (a non-modal
    // popup doesn't reset on a same-id OpenPopup while already open): commit the OLD
    // target's pending note-suggestion before starting the new cell on the general note.
    if (app.noteTargetValid && (app.noteCommitStudent != si || app.noteCommitQuestion != qi)) {
        app.commitPendingNoteSuggestion();
        app.editorNoteSub = -1;
    }

    gt::Student&  s = app.project.students[static_cast<size_t>(si)];
    gt::Question& q = app.project.questions[static_cast<size_t>(qi)];
    gt::Cell&     c = s.cells[static_cast<size_t>(qi)];
    if (q.subCount < 2)
        app.editorNoteSub = -1; // no ambiguity to disambiguate -> always the general note

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
    const double step = app.config.prefs.stepSize; // configurable +/- step (Settings)
    if (ImGui::Button("-##award")) { gt::stepAwarded(q, c, -step); app.markDirty(); }
    ImGui::SameLine();
    if (ImGui::Button("+##award")) { gt::stepAwarded(q, c, +step); app.markDirty(); }
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
    // Note (Hebrew / RTL aware) — the bidiNoteInput widget (ui/BidiInput.*) reuses
    // ImGui's editing engine but repaints in BiDi visual order: Hebrew flows right-to-
    // left, English left-to-right, paired punctuation mirrors in RTL runs, and the base
    // direction toggles with Ctrl+Left/Right-Shift. Stored text is exactly what's typed.
    ImGui::TextUnformatted("Note");

    // Target chips: [general] + one per sub-question, only shown when there's more
    // than one sub-question to disambiguate. Clicking a chip commits the OLD target's
    // pending suggestion (a settle boundary) before switching.
    if (q.subCount >= 2) {
        pushNotesFont();
        auto chip = [&](const std::string& label, int target) {
            const bool active = app.editorNoteSub == target;
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            const std::string id = label + "###tgt" + std::to_string(target);
            if (ImGui::Button(id.c_str())) {
                app.commitPendingNoteSuggestion();
                app.editorNoteSub = target;
            }
            if (active)
                ImGui::PopStyleColor(2);
        };
        chip("general", -1);
        for (int k = 0; k < q.subCount; ++k) {
            ImGui::SameLine();
            chip(noteVisual(gt::subHeader(q, k), gt::TextDir::Auto), k);
        }
        popNotesFont();
    }

    const int noteSub = app.editorNoteSub;
    if (noteSub < 0) {
        // General (whole-cell) note: binds the model fields directly.
        // Test aid only: BODEX_FOCUS_NOTE focuses the note field when the editor
        // opens, so scripted keystroke tests land in the note (default UX still
        // focuses grading). ID stays "##note" so this keeps working.
        static const bool kFocusNote = [] { const char* e = std::getenv("BODEX_FOCUS_NOTE"); return e && *e && *e != '0'; }();
        if (kFocusNote && ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if (gt::ui::bidiNoteInput("##note", c.note, c.noteDir, 280.0f)) {
            c.touched = true;
            app.markDirty();
        }
    } else {
        // Sub-question note: copy-out/write-back (never creates an empty SubNote
        // entry just from drawing) with a per-target widget id so ImGui's active-edit
        // buffer can't bleed across targets when the chip selection changes.
        pushNotesFont();
        ImGui::TextUnformatted((noteVisual(gt::subHeader(q, noteSub), gt::TextDir::Auto) + ":").c_str());
        popNotesFont();
        ImGui::SameLine();
        const gt::SubNote* existing = gt::findSubNote(c, noteSub);
        std::string text = existing ? existing->text : std::string();
        gt::TextDir dir   = existing ? existing->dir  : gt::TextDir::Auto;
        char noteId[24];
        std::snprintf(noteId, sizeof(noteId), "##note_s%d", noteSub);
        if (gt::ui::bidiNoteInput(noteId, text, dir, 280.0f)) {
            gt::setSubNote(c, noteSub, text, dir);
            c.touched = true;
            app.markDirty();
        }
    }

    // Suggestions: previously-committed notes for this exact (question, target),
    // newest first, clickable to reuse. Hidden-label Selectable (note text may
    // contain "##", which would otherwise truncate a plain label) + drawlist text.
    {
        std::vector<int> idx;
        for (int i = static_cast<int>(q.noteSuggestions.size()) - 1; i >= 0; --i)
            if (q.noteSuggestions[static_cast<size_t>(i)].sub == noteSub)
                idx.push_back(i);
        if (!idx.empty()) {
            ImGui::TextDisabled("Suggestions:");
            const float rowH = ImGui::GetTextLineHeightWithSpacing();
            const float h = std::min<float>(static_cast<float>(idx.size()), 6.0f) * rowH + 4.0f;
            if (ImGui::BeginChild("##notesugg", ImVec2(280.0f, h), ImGuiChildFlags_Borders)) {
                pushNotesFont();
                for (int i : idx) {
                    const gt::NoteSuggestion& sug = q.noteSuggestions[static_cast<size_t>(i)];
                    char selId[24];
                    std::snprintf(selId, sizeof(selId), "##sugg%d", i);
                    if (ImGui::Selectable(selId, false, ImGuiSelectableFlags_DontClosePopups)) {
                        if (noteSub < 0) { c.note = sug.text; c.noteDir = sug.dir; }
                        else             gt::setSubNote(c, noteSub, sug.text, sug.dir);
                        c.touched = true;
                        app.markDirty();
                    }
                    const ImVec2 rmin = ImGui::GetItemRectMin();
                    const std::string vis = noteVisual(sug.text, sug.dir);
                    ImGui::GetWindowDrawList()->AddText(ImVec2(rmin.x + 2.0f, rmin.y),
                                                        ImGui::GetColorU32(ImGuiCol_Text), vis.c_str());
                }
                popNotesFont();
            }
            ImGui::EndChild();
        }
    }

    // Refresh the pending-suggestion latch with whatever target was drawn this
    // frame, so the next settle boundary (target switch / editor close / save)
    // offers ITS current text — not a stale one from a prior frame.
    app.noteCommitStudent  = si;
    app.noteCommitQuestion = qi;
    app.noteCommitSub      = noteSub;
    app.noteTargetValid    = true;

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

void openNotesWindow(App& app, int student, int question)
{
    for (NotesWin& w : app.notesWins) {
        if (w.student == student && w.question == question) {
            w.open = true;
            w.focusNext = true;
            return;
        }
    }
    NotesWin w;
    w.id = app.nextNotesWinId++;
    w.student = student;
    w.question = question;
    w.focusNext = true;
    app.notesWins.push_back(w);
}

namespace {

// Draw one notes-viewer window. Returns false if it should be closed this frame.
bool drawNotesWindow(App& app, NotesWin& w)
{
    if (w.student < 0 || w.student >= static_cast<int>(app.project.students.size()) ||
        w.question < 0 || w.question >= static_cast<int>(app.project.questions.size()))
        return false;
    gt::Student&  s = app.project.students[static_cast<size_t>(w.student)];
    gt::Question& q = app.project.questions[static_cast<size_t>(w.question)];
    gt::Cell&     c = s.cells[static_cast<size_t>(w.question)];

    ImGui::SetNextWindowSize(ImVec2(380, 220), ImGuiCond_FirstUseEver);
    // Fixed ###id per window so its visible title (student/question names, which
    // can't actually change here, but mirrors the image-preview precedent) doesn't
    // make ImGui treat it as a different window.
    const std::string title = "Notes - Student " + std::to_string(s.id) + " - " + q.title +
        "###bodex_notes_" + std::to_string(w.id);

    bool open = true;
    if (ImGui::Begin(title.c_str(), &open)) {
        if (w.focusNext) { ImGui::SetWindowFocus(); w.focusNext = false; }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            app.anyPreviewFocused = true; // grid keyboard yields to a focused notes window too

        const std::vector<std::string> lines = cellNoteLinesVisual(q, c);
        if (lines.empty()) {
            ImGui::TextDisabled("(no notes)"); // stays open if the notes are cleared elsewhere
        } else {
            pushNotesFont();
            for (const auto& line : lines)
                ImGui::TextUnformatted(line.c_str());
            popNotesFont();
        }
    }
    ImGui::End();
    return open;
}

} // namespace

void notesViewerWindows(App& app)
{
    // Same "Unsaved Changes" modal guard as imagePreviewWindows (see NOTES.md /
    // spec §9b): a focused floating window would otherwise render above the modal
    // and swallow its Save/Discard/Cancel clicks.
    if (ImGui::IsPopupOpen("Unsaved Changes"))
        return;

    for (NotesWin& w : app.notesWins)
        if (w.open && !drawNotesWindow(app, w))
            w.open = false;

    app.notesWins.erase(
        std::remove_if(app.notesWins.begin(), app.notesWins.end(),
                       [](const NotesWin& w) { return !w.open; }),
        app.notesWins.end());
}

} // namespace gt::ui
