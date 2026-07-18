#pragma once

// Small shared ImGui helpers used across screens.

#include <string>
#include <vector>
#include "imgui.h"

namespace gt { enum class TextDir; struct Question; struct Cell; } // defined in model/Project.h

namespace gt::ui {

// Format a double compactly: up to 2 decimals with trailing zeros trimmed
// ("12", "12.5", "7.25").
std::string fmtNum(double v);

// A checkbox styled green to read as a "full marks" tick. Returns true when the
// value was toggled this frame.
bool greenTickCheckbox(const char* label, bool* v);

// One question's structure-config controls (title / points / sub-questions /
// Equal-Custom split / sub-points / optional sub-question labels), as drawn on the
// New Project and Project Settings screens. `index` is the 0-based column, used only
// for the "Q<n>" label. Normalizes the question after any structural edit; returns
// true if anything changed this frame (caller marks dirty). Caller owns the PushID.
bool questionConfigBlock(gt::Question& q, int index);

// Draw text using the current font family at a larger pixel size.
void bigTitle(const char* text, float px = 26.0f);

// The default ImGui font (ProggyClean) is ASCII-only, so cell notes use a separate
// Hebrew-capable font loaded at startup (main.cpp). setNotesFont registers it;
// push/popNotesFont wrap the note display/edit widgets. A null font falls back to
// the current font (Hebrew just won't render), so callers stay unconditional.
void    setNotesFont(ImFont* f);
ImFont* notesFont();
void    pushNotesFont();
void    popNotesFont();

// Reorder a note's logical (as-typed) text into visual (as-drawn) order for the
// given direction — used for read-only display (the grid hover tooltip). Pair with
// pushNotesFont() when drawing the result.
std::string noteVisual(const std::string& logicalUtf8, gt::TextDir dir);

// All of a cell's notes as display-ready (visual-order) lines: the general note
// first (no prefix, if set), then each per-sub-question note as "<header>: <text>".
// Empty if the cell has no notes. Used by the grid hover tooltip and the notes
// viewer window; pair with pushNotesFont() at the call site.
std::vector<std::string> cellNoteLinesVisual(const gt::Question& q, const gt::Cell& c);

} // namespace gt::ui
