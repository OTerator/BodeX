#include "ui/BidiInput.h"

#include "ui/widgets.h"       // pushNotesFont / popNotesFont
#include "model/Project.h"    // gt::TextDir
#include "model/Bidi.h"
#include "imgui_stdlib.h"     // ImGui::InputText(std::string*)

#include <algorithm>
#include <cmath>
#include <vector>

namespace gt::ui {

namespace {

gt::BaseDir baseDirOf(gt::TextDir d)
{
    switch (d) {
        case gt::TextDir::LTR: return gt::BaseDir::LTR;
        case gt::TextDir::RTL: return gt::BaseDir::RTL;
        default:               return gt::BaseDir::Auto;
    }
}

// Per-widget transient state, keyed by the field's ImGui id (only one note edits at
// a time, but keying keeps it correct if that ever changes). `scrollX` is our own
// horizontal scroll; the override* fields are set by our mouse/keyboard handling
// before InputText and applied to ImGui's edit cursor inside the callback.
struct EditState {
    ImGuiID id = 0;
    float   scrollX = 0.0f;
    bool    active  = false;   // was focused last frame (gate keyboard intents)

    bool    setCursor = false; // apply overrideCursor in the callback
    int     overrideCursor = 0;
    bool    setSelect = false; // apply overrideSel* in the callback
    int     overrideSelStart = 0, overrideSelEnd = 0;

    bool    dragging = false;
    int     dragAnchor = 0;

    // Read back from the callback each frame (UTF-8 byte offsets).
    int     curCursor = 0, curSelStart = 0, curSelEnd = 0;
};
EditState g_st;

EditState& stateFor(ImGuiID id)
{
    if (g_st.id != id) { g_st = EditState(); g_st.id = id; }
    return g_st;
}

// Callback: apply any pending cursor/selection override (from our mouse/nav), then
// read back the resulting cursor/selection for this frame's drawing.
int editCallback(ImGuiInputTextCallbackData* d)
{
    EditState* st = static_cast<EditState*>(d->UserData);
    if (st->setCursor) {
        d->CursorPos = st->overrideCursor;
        if (!st->setSelect) { d->SelectionStart = d->SelectionEnd = st->overrideCursor; }
    }
    if (st->setSelect) {
        d->SelectionStart = st->overrideSelStart;
        d->SelectionEnd   = st->overrideSelEnd;
    }
    st->setCursor = st->setSelect = false;
    st->curCursor   = d->CursorPos;
    st->curSelStart = d->SelectionStart;
    st->curSelEnd   = d->SelectionEnd;
    return 0;
}

// BiDi layout of one note: codepoints + their byte offsets, the reorder result, and
// per-visual-glyph x edges (content space, 0..total) measured in the current font.
struct Layout {
    std::u32string     cps;
    std::vector<int>   cpByte;   // size ncp+1; byte offset of each cp, last = size()
    gt::BidiResult     bidi;
    std::vector<float> vx;       // size nvis+1; left edge of each visual glyph
    float total = 0.0f;
    int   ncp = 0, nvis = 0;
};

Layout buildLayout(const std::string& text, gt::TextDir dir)
{
    Layout L;
    // Decode UTF-8 to codepoints while recording each codepoint's byte offset.
    const size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        L.cpByte.push_back(static_cast<int>(i));
        unsigned char c = static_cast<unsigned char>(text[i]);
        int extra = (c < 0x80) ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : 0;
        char32_t cp = c < 0x80 ? c : (c & (0x7F >> (extra + 1)));
        for (int k = 1; k <= extra && i + k < n; ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        L.cps.push_back(cp);
        i += extra + 1;
    }
    L.cpByte.push_back(static_cast<int>(n)); // sentinel end offset
    L.bidi = gt::bidiReorder(L.cps, baseDirOf(dir));
    L.ncp = static_cast<int>(L.cps.size());
    L.nvis = static_cast<int>(L.bidi.visual.size());
    L.vx.assign(L.nvis + 1, 0.0f);
    for (int v = 0; v < L.nvis; ++v) {
        const std::string g = gt::codepointsToUtf8(std::u32string(1, L.bidi.visual[static_cast<size_t>(v)]));
        L.vx[v + 1] = L.vx[v] + ImGui::CalcTextSize(g.c_str()).x; // no kerning -> per-glyph sums
    }
    L.total = L.vx[L.nvis];
    return L;
}

int byteToCp(const Layout& L, int byte)
{
    for (int p = 0; p <= L.ncp; ++p)
        if (L.cpByte[static_cast<size_t>(p)] == byte) return p;
    return (byte <= 0) ? 0 : L.ncp;
}
int cpToByte(const Layout& L, int p) { return L.cpByte[static_cast<size_t>(std::clamp(p, 0, L.ncp))]; }

// Caret x (content space) for logical caret position p in [0,ncp]: the leading edge
// of char p (trailing edge of the last char at the end), choosing the side by that
// glyph's resolved direction so the caret sits correctly in LTR and RTL runs.
float caretCX(const Layout& L, int p, bool baseRtl)
{
    if (L.ncp == 0) return baseRtl ? L.total : 0.0f;
    if (p <= 0) {
        int v = L.bidi.logicalToVisual[0];
        return L.bidi.visualRtl[static_cast<size_t>(v)] ? L.vx[v + 1] : L.vx[v];
    }
    if (p >= L.ncp) {
        int v = L.bidi.logicalToVisual[static_cast<size_t>(L.ncp - 1)];
        return L.bidi.visualRtl[static_cast<size_t>(v)] ? L.vx[v] : L.vx[v + 1];
    }
    int v = L.bidi.logicalToVisual[static_cast<size_t>(p)];
    return L.bidi.visualRtl[static_cast<size_t>(v)] ? L.vx[v + 1] : L.vx[v];
}

// Nearest logical caret position to a content-space x (inverse of caretCX).
int hitTestCp(const Layout& L, float cx, bool baseRtl)
{
    int best = 0; float bestD = 1e30f;
    for (int p = 0; p <= L.ncp; ++p) {
        float d = std::fabs(caretCX(L, p, baseRtl) - cx);
        if (d < bestD) { bestD = d; best = p; }
    }
    return best;
}

// Move the caret one glyph in the on-screen direction dir (-1 = left, +1 = right):
// pick the nearest caret position whose x is strictly to that side.
int moveVisual(const Layout& L, int p, int dir, bool baseRtl)
{
    const float cur = caretCX(L, p, baseRtl);
    int best = p; float bestD = 1e30f;
    for (int q = 0; q <= L.ncp; ++q) {
        if (q == p) continue;
        float x = caretCX(L, q, baseRtl);
        float d = (dir < 0) ? (cur - x) : (x - cur);
        if (d > 0.001f && d < bestD) { bestD = d; best = q; }
    }
    return best;
}

// Compute the screen x of content-x 0 (left edge of the visual block): right-aligned
// when RTL and fitting, else left-aligned, with caret-following horizontal scroll.
float computeOrigin(EditState& st, const Layout& L, float viewLeft, float viewRight,
                    int curCp, bool baseRtl, bool active)
{
    const float avail = viewRight - viewLeft;
    if (L.total <= avail) {
        st.scrollX = 0.0f;
        return baseRtl ? (viewRight - L.total) : viewLeft;
    }
    if (active) {
        const float cx = caretCX(L, curCp, baseRtl);
        if (cx < st.scrollX)          st.scrollX = cx;
        if (cx > st.scrollX + avail)  st.scrollX = cx - avail;
    } else {
        st.scrollX = baseRtl ? (L.total - avail) : 0.0f; // show the text's start
    }
    st.scrollX = std::clamp(st.scrollX, 0.0f, L.total - avail);
    return viewLeft - st.scrollX;
}

} // namespace

bool bidiNoteInput(const char* id, std::string& text, gt::TextDir& dir, float width, bool compact)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiID wid = ImGui::GetID(id);
    EditState& st = stateFor(wid);
    bool changed = false;

    pushNotesFont();
    const float fontSize = ImGui::GetFontSize();

    // Real colors, captured before we hide ImGui's native rendering.
    const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 colSel  = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
    const ImU32 colHint = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float  frameH = ImGui::GetFrameHeight();
    const bool   showClear = !text.empty();
    const float  clearW = showClear ? frameH : 0.0f;
    const float  spacing = showClear ? style.ItemInnerSpacing.x : 0.0f;
    const float  fieldW = std::max(20.0f, width - clearW - spacing);

    const float viewLeft  = pos.x + style.FramePadding.x;
    const float viewRight = pos.x + fieldW - style.FramePadding.x;
    const float textY     = pos.y + style.FramePadding.y;
    const bool  baseRtl   = gt::bidiBaseIsRtl(text, baseDirOf(dir));

    // --- Layout of the current (pre-edit) text, for hit-testing this frame. ---
    Layout L = buildLayout(text, dir);
    int curCp = byteToCp(L, std::clamp(st.curCursor, 0, static_cast<int>(text.size())));
    float originX = computeOrigin(st, L, viewLeft, viewRight, curCp, baseRtl, st.active);

    // --- Mouse: click positions the caret, drag extends the selection (our layout). ---
    const bool hovered = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + fieldW, pos.y + frameH));
    if (hovered && ImGui::IsMouseClicked(0) && !ImGui::IsMouseDoubleClicked(0)) {
        int p = hitTestCp(L, io.MousePos.x - originX, baseRtl);
        st.setCursor = true; st.overrideCursor = cpToByte(L, p);
        st.setSelect = false; st.dragging = true; st.dragAnchor = st.overrideCursor;
    } else if (st.dragging && ImGui::IsMouseDown(0)) {
        int p = hitTestCp(L, io.MousePos.x - originX, baseRtl);
        int b = cpToByte(L, p);
        st.setCursor = true; st.overrideCursor = b;
        st.setSelect = true; st.overrideSelStart = st.dragAnchor; st.overrideSelEnd = b;
    }
    if (ImGui::IsMouseReleased(0)) st.dragging = false;

    // --- Visual arrow navigation: plain Left/Right move by screen direction. ---
    if (st.active && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
            st.setCursor = true; st.overrideCursor = cpToByte(L, moveVisual(L, curCp, -1, baseRtl));
        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
            st.setCursor = true; st.overrideCursor = cpToByte(L, moveVisual(L, curCp, +1, baseRtl));
        }
    }

    // --- The hidden InputText: does all real editing; we zero its rendering. ---
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_InputTextCursor, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(0, 0, 0, 0));
    ImGui::SetNextItemWidth(fieldW);
    if (ImGui::InputText(id, &text, ImGuiInputTextFlags_CallbackAlways, editCallback, &st))
        changed = true;
    ImGui::PopStyleColor(3);
    const bool active = ImGui::IsItemActive();
    st.active = active;

    // --- Ctrl+Left/Right-Shift set the base direction while focused. ---
    if (active && io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftShift, false))  { dir = gt::TextDir::LTR; changed = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_RightShift, false)) { dir = gt::TextDir::RTL; changed = true; }
    }

    // --- Repaint in visual order (rebuild layout if the text changed). ---
    if (changed) L = buildLayout(text, dir);
    curCp = byteToCp(L, std::clamp(st.curCursor, 0, static_cast<int>(text.size())));
    originX = computeOrigin(st, L, viewLeft, viewRight, curCp, baseRtl, active);

    dl->PushClipRect(ImVec2(pos.x, pos.y), ImVec2(pos.x + fieldW, pos.y + frameH), true);
    if (L.ncp == 0 && !active) {
        dl->AddText(ImVec2(viewLeft, textY), colHint, "Add a note...");
    } else {
        // Selection highlight (per selected glyph; adjacent rects read as one run).
        if (active && st.curSelStart != st.curSelEnd) {
            int a = byteToCp(L, std::min(st.curSelStart, st.curSelEnd));
            int b = byteToCp(L, std::max(st.curSelStart, st.curSelEnd));
            for (int p = a; p < b && p < L.ncp; ++p) {
                int v = L.bidi.logicalToVisual[static_cast<size_t>(p)];
                dl->AddRectFilled(ImVec2(originX + L.vx[v], textY),
                                  ImVec2(originX + L.vx[v + 1], textY + fontSize), colSel);
            }
        }
        const std::string vis = gt::codepointsToUtf8(L.bidi.visual);
        dl->AddText(ImVec2(originX, textY), colText, vis.c_str());
        // Caret (blinking) when focused.
        if (active) {
            bool blink = std::fmod(static_cast<float>(ImGui::GetTime()), 1.06f) <= 0.66f;
            if (blink) {
                float cx = originX + caretCX(L, curCp, baseRtl);
                dl->AddRectFilled(ImVec2(cx, textY - 1.0f), ImVec2(cx + 1.0f, textY + fontSize + 1.0f), colText);
            }
        }
    }
    dl->PopClipRect();

    // --- Inline clear (x) button. ---
    if (showClear) {
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("x##noteclear", ImVec2(clearW, frameH))) { text.clear(); st = EditState(); st.id = wid; changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear note");
    }

    // --- Direction control + resolved-direction indicator (skipped when compact). ---
    if (!compact) {
        ImGui::TextDisabled("dir:");
        ImGui::SameLine();
        int di = static_cast<int>(dir);
        ImGui::RadioButton("Auto##notedir", &di, 0); ImGui::SameLine();
        ImGui::RadioButton("LTR##notedir",  &di, 1); ImGui::SameLine();
        ImGui::RadioButton("RTL##notedir",  &di, 2); ImGui::SameLine();
        if (di != static_cast<int>(dir)) { dir = static_cast<gt::TextDir>(di); changed = true; }
        ImGui::TextDisabled(baseRtl ? "reads R-to-L   (Ctrl+Shift toggles)"
                                    : "reads L-to-R   (Ctrl+Shift toggles)");
    }

    popNotesFont();
    return changed;
}

} // namespace gt::ui
