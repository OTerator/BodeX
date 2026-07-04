#include "ui/GradingTable.h"

#include "app/App.h"
#include "ui/widgets.h"
#include "ui/CellEditor.h"
#include "ui/QuestionImages.h"
#include "model/Scoring.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace gt::ui {

namespace {

const ImVec4 kGreenBtn (0.16f, 0.42f, 0.20f, 1.0f);
const ImVec4 kGreenBtnH(0.22f, 0.54f, 0.26f, 1.0f);
const ImVec4 kGreenBtnA(0.26f, 0.62f, 0.30f, 1.0f);
const ImVec4 kOverBtn  (0.48f, 0.24f, 0.16f, 1.0f); // awarded over max
const ImVec4 kMutedText(0.55f, 0.57f, 0.60f, 1.0f);
const ImU32  kNoSubRowBg = IM_COL32(70, 55, 55, 90);

// Screen rectangles of the grade cells drawn this frame (non-no-submission
// only), used to hit-test the mouse for the drag-to-paint gesture. We cannot
// rely on ImGui::IsItemHovered() during the drag because the pressed cell button
// captures hover while held. Cleared at the start of every grading frame.
struct CellRect { ImVec2 mn, mx; int row, col; };
std::vector<CellRect> g_cellRects;

// Index into g_cellRects of the cell under the mouse, or -1.
int cellUnderMouse()
{
    const ImVec2 m = ImGui::GetMousePos();
    for (size_t k = 0; k < g_cellRects.size(); ++k) {
        const CellRect& cr = g_cellRects[k];
        if (m.x >= cr.mn.x && m.x < cr.mx.x && m.y >= cr.mn.y && m.y < cr.mx.y)
            return static_cast<int>(k);
    }
    return -1;
}

// Two-line summary shown inside a grade cell:
//   line 1: score + sub-question tally (X/Y)
//   line 2: last page (standalone, so it reads clearly)
std::string cellSummary(const gt::Question& q, const gt::Cell& c)
{
    std::string line1;
    char b[48];
    if (c.fullTick) {
        // Full tick => full points and, by definition, all sub-questions answered.
        std::snprintf(b, sizeof(b), "FULL %s  %d/%d", fmtNum(q.maxPoints).c_str(), q.subCount, q.subCount);
        line1 = b;
    } else if (!c.touched) {
        line1 = "-";
    } else {
        std::snprintf(b, sizeof(b), "%s  %d/%d", fmtNum(c.awarded).c_str(), c.subAnswered, q.subCount);
        line1 = b;
    }
    // Second line: last page, prefixed with "lp:" so a bare number reads clearly.
    std::string line2 = c.lastPage.empty() ? std::string() : ("lp: " + c.lastPage);
    return line1 + "\n" + line2;
}

void renderGradeCell(App& app, int i, int j)
{
    gt::Student&  s = app.project.students[static_cast<size_t>(i)];
    gt::Question& q = app.project.questions[static_cast<size_t>(j)];
    gt::Cell&     c = s.cells[static_cast<size_t>(j)];

    int pushed = 0;
    if (c.fullTick) {
        ImGui::PushStyleColor(ImGuiCol_Button, kGreenBtn);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kGreenBtnH);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kGreenBtnA);
        pushed = 3;
    } else if (gt::cellOverMax(q, c)) {
        ImGui::PushStyleColor(ImGuiCol_Button, kOverBtn);
        pushed = 1;
    }

    char id[32];
    std::snprintf(id, sizeof(id), "##cell_%d_%d", i, j);
    const std::string label = cellSummary(q, c) + id;

    // Two text lines tall so the last page gets its own line.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float cellH = ImGui::GetTextLineHeight() * 2.0f + style.FramePadding.y * 2.0f + 4.0f;

    if (s.noSubmission) ImGui::BeginDisabled();
    ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, cellH));
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();
    const bool leftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool hovered     = ImGui::IsItemHovered();
    if (s.noSubmission) ImGui::EndDisabled();

    // Interaction (swapped from the usual convention on purpose):
    //   left-click  -> open the cell editor (occlusion-aware via IsItemClicked)
    //   right-click / right-drag -> full marks (handlePaintGesture, after the table)
    // The left-click editor uses IsItemClicked so it won't fire for cells hidden
    // under the floating image-preview window. (See NOTES.md: preview click-through.)
    if (!s.noSubmission) {
        g_cellRects.push_back({ rmin, rmax, i, j });
        if (leftClicked) {
            app.activeRow = i;         // keep the keyboard selection in sync
            app.activeCol = j;
            app.gridEditing = false;
            app.editorStudent = i;
            app.editorQuestion = j;
            app.requestOpenCellEditor = true;
        }
    }

    if (!s.noSubmission && hovered && (c.touched || c.fullTick) && !c.note.empty())
        ImGui::SetTooltip("%s", c.note.c_str());

    if (pushed) ImGui::PopStyleColor(pushed);

    // Keyboard-selection highlight + scroll-into-view. Drawn for no-submission
    // cells too, so the selection stays visible while toggling a whole NS row.
    if (i == app.activeRow && j == app.activeCol) {
        ImGui::GetWindowDrawList()->AddRect(rmin, rmax, IM_COL32(90, 160, 255, 255), 0.0f, 0, 2.5f);
        if (app.gridScrollToActive) {
            ImGui::SetScrollHereY(0.5f);
            ImGui::SetScrollHereX(0.5f);
            app.gridScrollToActive = false;
        }
    }
}

// The inline editor seeds its buffer with the digit that started it, then focuses
// via SetKeyboardFocusHere -- which selects-all the seed, so the next keystroke
// would replace it. This callback collapses that initial selection once (detected
// by a live selection span), so subsequent digits append. Timing-robust: it only
// fires on the frame the auto-selection is actually present.
int inlineEditCallback(ImGuiInputTextCallbackData* data)
{
    App* app = static_cast<App*>(data->UserData);
    if (app->gridEditDeselect && data->SelectionStart != data->SelectionEnd) {
        data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
        app->gridEditDeselect = false;
    }
    return 0;
}

// Inline numeric quick-entry drawn in place of the active cell's button while
// gridEditing is on. Type digits to set awarded points; Enter commits and moves
// down, Tab commits and moves right, Esc discards. Only used for non-no-submission
// cells (the caller guarantees this).
void renderInlineEdit(App& app, int i, int j)
{
    gt::Cell&     c = app.project.students[static_cast<size_t>(i)].cells[static_cast<size_t>(j)];
    gt::Question& q = app.project.questions[static_cast<size_t>(j)];
    const int N = static_cast<int>(app.project.students.size());
    const int M = static_cast<int>(app.project.questions.size());

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (app.gridEditFocus) {
        ImGui::SetKeyboardFocusHere();
        app.gridEditFocus = false;
    }
    const bool enter = ImGui::InputText("##inline", &app.gridEditBuf,
        ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_CallbackAlways, inlineEditCallback, &app);
    const bool tab = ImGui::IsKeyPressed(ImGuiKey_Tab);
    const bool esc = ImGui::IsKeyPressed(ImGuiKey_Escape);

    // Record the rect (so the paint hit-test still sees a cell here) + highlight.
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();
    g_cellRects.push_back({ rmin, rmax, i, j });
    ImGui::GetWindowDrawList()->AddRect(rmin, rmax, IM_COL32(90, 160, 255, 255), 0.0f, 0, 2.5f);
    if (app.gridScrollToActive) {
        ImGui::SetScrollHereY(0.5f);
        ImGui::SetScrollHereX(0.5f);
        app.gridScrollToActive = false;
    }

    if (esc) {                     // discard: leave the cell as it was
        app.gridEditing = false;
        return;
    }
    if (enter || tab) {
        double v = std::strtod(app.gridEditBuf.c_str(), nullptr); // "" -> 0
        const double em = gt::effectiveMax(q, c);  // can't inline past the locked ceiling
        if (v < 0.0) v = 0.0;
        if (v > em)  v = em;
        c.awarded = v;
        c.fullTick = false;        // an explicit number governs over a full tick
        c.touched = true;
        app.markDirty();
        app.gridEditing = false;
        if (tab) { if (app.activeCol < M - 1) ++app.activeCol; }
        else     { if (app.activeRow < N - 1) ++app.activeRow; }
        app.gridScrollToActive = true;
    }
}

void renderIdCell(App& app, int i)
{
    gt::Student& s = app.project.students[static_cast<size_t>(i)];

    char label[48];
    if (s.noSubmission)
        std::snprintf(label, sizeof(label), "%d (NS)##id_%d", s.id, i);
    else
        std::snprintf(label, sizeof(label), "%d##id_%d", s.id, i);

    if (s.noSubmission) ImGui::PushStyleColor(ImGuiCol_Text, kMutedText);
    if (ImGui::Selectable(label, false)) {
        app.menuStudent = i;
        app.requestOpenStudentMenu = true;
    }
    if (s.noSubmission) ImGui::PopStyleColor();
}

// Set full marks (never unset) on every cell from the anchor to (hr,hc) along the
// locked axis (1 = row, 2 = column). Skips no-submission students.
void applyFullRange(App& app, int anchorRow, int anchorCol, int hr, int hc, int axis)
{
    gt::Project& p = app.project;
    const int nStudents  = static_cast<int>(p.students.size());
    const int nQuestions = static_cast<int>(p.questions.size());
    bool changed = false;

    auto paint = [&](int r, int cIdx) {
        if (r < 0 || r >= nStudents || cIdx < 0 || cIdx >= nQuestions) return;
        if (p.students[static_cast<size_t>(r)].noSubmission) return;
        gt::Cell& cell = p.students[static_cast<size_t>(r)].cells[static_cast<size_t>(cIdx)];
        // Only flip fullTick; leave `touched`/awarded alone so un-fulling a blank
        // cell returns it to blank (full cells count regardless of `touched`).
        if (!cell.fullTick) { cell.fullTick = true; changed = true; }
    };

    if (axis == 1) {                       // row: fixed anchorRow, cols anchorCol..hc
        const int c0 = std::min(anchorCol, hc), c1 = std::max(anchorCol, hc);
        for (int c = c0; c <= c1; ++c) paint(anchorRow, c);
    } else if (axis == 2) {                // column: fixed anchorCol, rows anchorRow..hr
        const int r0 = std::min(anchorRow, hr), r1 = std::max(anchorRow, hr);
        for (int r = r0; r <= r1; ++r) paint(r, anchorCol);
    }
    if (changed) app.markDirty();
}

// Resolve the RIGHT-click / drag-to-paint full-marks gesture using the cell rects
// recorded this frame. A plain right-click toggles the cell's full marks; a
// right-drag paints full marks along the row or column (axis from the initial
// drag direction). Left-click opens the editor (handled per-cell in renderGradeCell).
void handlePaintGesture(App& app)
{
    // Ignore while any popup (cell editor / student menu / save prompt) is open.
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        app.paintActive = false;
        return;
    }

    // Ignore when the mouse is over a floating window (e.g. an image preview) that
    // occludes the grid. This gesture hit-tests recorded cell rects by position
    // (g_cellRects) and is otherwise blind to z-order, so without this gate,
    // dragging a preview across the grid would paint the cells hidden beneath it.
    // Called while the "Grading" window is current, so this queries it (+ its
    // table child). See NOTES.md "image-preview click-through".
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        app.paintActive = false;
        return;
    }

    gt::Project& p = app.project;
    const int idx = cellUnderMouse();
    const bool over = idx >= 0;
    const int hr = over ? g_cellRects[static_cast<size_t>(idx)].row : -1;
    const int hc = over ? g_cellRects[static_cast<size_t>(idx)].col : -1;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && over) {
        app.paintActive = true;
        app.paintIsDrag = false;
        app.paintAnchorRow = hr;
        app.paintAnchorCol = hc;
        app.paintAxis = 0;
    }

    if (app.paintActive && ImGui::IsMouseDown(ImGuiMouseButton_Right) && over) {
        if (hr != app.paintAnchorRow || hc != app.paintAnchorCol) {
            app.paintIsDrag = true;
            if (app.paintAxis == 0) {           // lock the axis on first cell change
                if (hr == app.paintAnchorRow)      app.paintAxis = 1;
                else if (hc == app.paintAnchorCol) app.paintAxis = 2;
                else app.paintAxis = (std::abs(hc - app.paintAnchorCol) >= std::abs(hr - app.paintAnchorRow)) ? 1 : 2;
            }
        }
        if (app.paintIsDrag)
            applyFullRange(app, app.paintAnchorRow, app.paintAnchorCol, hr, hc, app.paintAxis);
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && app.paintActive) {
        // A plain click (no drag, released over the same cell) toggles full marks.
        if (!app.paintIsDrag && over && hr == app.paintAnchorRow && hc == app.paintAnchorCol &&
            hr >= 0 && hr < static_cast<int>(p.students.size()) &&
            hc >= 0 && hc < static_cast<int>(p.questions.size())) {
            gt::Cell& cell = p.students[static_cast<size_t>(hr)].cells[static_cast<size_t>(hc)];
            cell.fullTick = !cell.fullTick; // leave `touched` so un-fulling a blank cell stays blank
            app.markDirty();
        }
        app.paintActive = false;
        app.paintIsDrag = false;
        app.paintAxis = 0;
    }
}

// Keyboard-first grading. Runs before the table is drawn so the selection, scroll
// and inline-edit reflect the new state this frame. Clamps the active cell, then
// (while the grading grid owns the keyboard) applies the keymap shown in the
// status bar. ImGui's own keyboard nav is disabled on this screen in App::render,
// so arrows/Tab don't double-drive focus here.
void handleGridKeyboard(App& app)
{
    gt::Project& p = app.project;
    const int N = static_cast<int>(p.students.size());
    const int M = static_cast<int>(p.questions.size());
    if (N == 0 || M == 0)
        return;

    // Keep the selection in-bounds (defensive; grid shape is otherwise stable).
    app.activeRow = std::clamp(app.activeRow, 0, N - 1);
    app.activeCol = std::clamp(app.activeCol, 0, M - 1);

    // Only drive the grid while it owns the keyboard: no popup up, and no floating
    // image-preview window focused (a focused preview handles its own keys — see
    // QuestionImages.cpp). When the app isn't the OS-foreground window it receives
    // no key events at all, so nothing fires regardless.
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return;
    if (app.anyPreviewFocused)
        return;

    // While inline-editing the keys belong to the InputText (caret + commit/cancel
    // are handled in renderInlineEdit).
    if (app.gridEditing)
        return;

    ImGuiIO& io = ImGui::GetIO();
    int& r = app.activeRow;
    int& c = app.activeCol;
    bool moved = false;

    // ---- navigation (arrows / Tab / Enter) ----
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))     { if (r > 0)     { --r; moved = true; } }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) { if (r < N - 1) { ++r; moved = true; } }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))   { if (c > 0)     { --c; moved = true; } }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))  { if (c < M - 1) { ++c; moved = true; } }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        if (io.KeyShift) { if (c > 0)     { --c; moved = true; } }
        else             { if (c < M - 1) { ++c; moved = true; } }
    }

    gt::Student& s    = p.students[static_cast<size_t>(r)];
    gt::Cell&    cell = s.cells[static_cast<size_t>(c)];

    // ---- single-key actions (no key-repeat) ----
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {           // open the full editor
        app.editorStudent = r;
        app.editorQuestion = c;
        app.requestOpenCellEditor = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !s.noSubmission) {
        cell.fullTick = !cell.fullTick;                      // leave `touched` alone (§6)
        app.markDirty();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        s.noSubmission = !s.noSubmission;
        app.markDirty();
    }
    if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && !s.noSubmission) {
        cell = gt::blankCell(p.questions[static_cast<size_t>(c)]); // blank, all-answered
        app.markDirty();
    }

    // ---- type a digit/./- to begin inline awarded-points entry ----
    // Seed the buffer with the typed character(s) and consume them from the queue,
    // so the first digit isn't lost while the InputText is being focused. The
    // seed's auto-select-all is collapsed by inlineEditCallback so later digits
    // append rather than replace.
    if (!s.noSubmission && !io.InputQueueCharacters.empty()) {
        std::string seed;
        for (ImWchar ch : io.InputQueueCharacters)
            if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-')
                seed.push_back(static_cast<char>(ch));
        if (!seed.empty()) {
            app.gridEditBuf = seed;
            app.gridEditing = true;
            app.gridEditFocus = true;
            app.gridEditDeselect = true;
            io.InputQueueCharacters.resize(0);
        }
    }

    if (moved)
        app.gridScrollToActive = true;
}

} // namespace

void gradingScreen(App& app)
{
    gt::Project& p = app.project;

    g_cellRects.clear(); // repopulated as grade cells are drawn this frame

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Grading", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ---- toolbar ----
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s%s", p.name.c_str(), app.dirty ? " *" : "");
    ImGui::SameLine();
    if (ImGui::SmallButton("Save"))    app.doSave();
    ImGui::SameLine();
    if (ImGui::SmallButton("Save As")) app.doSaveAs();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    const gt::ClassStats st = gt::classStats(p);
    ImGui::TextDisabled("students %d  graded %d  avg %s  min %s  max %s  (out of %s)",
                        st.students, st.graded, fmtNum(st.average).c_str(),
                        fmtNum(st.minScore).c_str(), fmtNum(st.maxScore).c_str(),
                        fmtNum(gt::projectMaxTotal(p)).c_str());

    // Keyboard-first grading: move the selection / begin inline entry / act on the
    // active cell. Before BeginTable so the changes take effect this frame.
    handleGridKeyboard(app);

    // ---- grid ----
    const int M = static_cast<int>(p.questions.size());
    const int columns = 1 + M + 1;

    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_HighlightHoveredColumn;

    const ImVec2 tableSize(0.0f, -ImGui::GetFrameHeightWithSpacing());

    if (ImGui::BeginTable("grid", columns, flags, tableSize)) {
        ImGui::TableSetupScrollFreeze(1, 1); // keep ID column + header visible

        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthFixed, 64.0f);
        // Header labels carry the max points; keep the strings alive for the call.
        std::vector<std::string> headers;
        headers.reserve(static_cast<size_t>(M));
        for (int j = 0; j < M; ++j)
            headers.push_back(p.questions[static_cast<size_t>(j)].title + " /" +
                              fmtNum(p.questions[static_cast<size_t>(j)].maxPoints));
        for (int j = 0; j < M; ++j)
            ImGui::TableSetupColumn(headers[static_cast<size_t>(j)].c_str(), ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 92.0f);

        // Custom header row. Mirrors ImGui::TableHeadersRow() (PushID per column +
        // proper header row height + the TableSetColumnIndex skip) so we don't
        // corrupt table state, while making question headers clickable to open
        // their image menu and showing a badge for attached images.
        const int headerCols = 1 + M + 1;
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers); // auto header row height
        for (int c = 0; c < headerCols; ++c) {
            if (!ImGui::TableSetColumnIndex(c))
                continue;
            ImGui::PushID(c);
            if (c >= 1 && c <= M) {
                const int j = c - 1;
                std::string hlabel = headers[static_cast<size_t>(j)];
                const size_t nImg = p.questions[static_cast<size_t>(j)].images.size();
                if (nImg > 0)
                    hlabel += "  [" + std::to_string(nImg) + " img]";
                ImGui::TableHeader(hlabel.c_str());
                // Header image menu opens on LEFT-click. (The left/right swap is
                // for cells only; right-click on a header is ImGui's built-in
                // "size column to fit" menu, so keeping this on left avoids a
                // collision.)
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    app.imageMenuQuestion = j;
                    app.requestOpenImageMenu = true;
                }
            } else {
                ImGui::TableHeader(ImGui::TableGetColumnName(c)); // "ID" / "Total"
            }
            ImGui::PopID();
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(p.students.size()));
        // Always submit the active row even when scrolled off, so it can be
        // highlighted and scrolled back into view.
        if (app.activeRow >= 0 && app.activeRow < static_cast<int>(p.students.size()))
            clipper.IncludeItemByIndex(app.activeRow);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                gt::Student& s = p.students[static_cast<size_t>(i)];
                ImGui::TableNextRow();
                if (s.noSubmission)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kNoSubRowBg);

                ImGui::TableSetColumnIndex(0);
                renderIdCell(app, i);

                for (int j = 0; j < M; ++j) {
                    ImGui::TableSetColumnIndex(1 + j);
                    if (app.gridEditing && i == app.activeRow && j == app.activeCol && !s.noSubmission)
                        renderInlineEdit(app, i, j);
                    else
                        renderGradeCell(app, i, j);
                }

                ImGui::TableSetColumnIndex(1 + M);
                ImGui::AlignTextToFramePadding();
                if (s.noSubmission)
                    ImGui::TextColored(kMutedText, "0 / %s", fmtNum(gt::projectMaxTotal(p)).c_str());
                else
                    ImGui::Text("%s / %s",
                                fmtNum(gt::studentTotal(p, static_cast<size_t>(i))).c_str(),
                                fmtNum(gt::projectMaxTotal(p)).c_str());
            }
        }

        ImGui::EndTable();
    }

    // Resolve the left-click / drag-to-paint full-marks gesture for this frame.
    handlePaintGesture(app);

    // ---- status bar ----
    ImGui::TextDisabled("%s", app.statusMsg.empty()
        ? "Arrows move; type = points; Enter/Tab commit; F2 edit; f full; n no-sub; Del clear.  Right-drag paints full marks.  Ctrl+S saves."
        : app.statusMsg.c_str());

    // ---- popups (opened here, after the table, using stored targets) ----
    if (app.requestOpenCellEditor) {
        ImGui::OpenPopup("CellEditor");
        app.requestOpenCellEditor = false;
    }
    if (app.requestOpenStudentMenu) {
        ImGui::OpenPopup("StudentMenu");
        app.requestOpenStudentMenu = false;
    }
    if (app.requestOpenImageMenu) {
        ImGui::OpenPopup("QuestionImages");
        app.requestOpenImageMenu = false;
    }
    cellEditorPopup(app);
    studentMenuPopup(app);
    questionImagesPopup(app);

    ImGui::End();

    // Image previews are separate, non-modal windows (several can stay open while
    // grading; navigate/zoom them without touching the header popup).
    imagePreviewWindows(app);
}

} // namespace gt::ui
