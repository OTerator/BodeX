#pragma once

// Small shared ImGui helpers used across screens.

#include <string>
#include "imgui.h"

namespace gt { enum class TextDir; } // defined in model/Project.h

namespace gt::ui {

// Format a double compactly: up to 2 decimals with trailing zeros trimmed
// ("12", "12.5", "7.25").
std::string fmtNum(double v);

// A checkbox styled green to read as a "full marks" tick. Returns true when the
// value was toggled this frame.
bool greenTickCheckbox(const char* label, bool* v);

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

// BiDi display helpers for cell notes: reorder logical (as-typed) text into visual
// (as-drawn) order for the given direction, and report whether it reads right-to-
// left (for alignment). Pair with pushNotesFont() when drawing the result.
std::string noteVisual(const std::string& logicalUtf8, gt::TextDir dir);
bool        noteIsRtl (const std::string& logicalUtf8, gt::TextDir dir);

} // namespace gt::ui
