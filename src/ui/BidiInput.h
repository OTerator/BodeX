#pragma once

// A single-line, bidirectional (Hebrew/RTL-aware) text field for cell notes.
//
// It wraps ImGui's own InputText editing engine so all the mature behaviour —
// insertion, delete, selection, clipboard, IME, undo, keyboard — runs unchanged on
// the *logical* UTF-8 buffer (what you type is exactly what is stored). The field's
// native rendering is made invisible; we repaint the text, caret and selection in
// BiDi *visual* order (mirroring paired punctuation in RTL runs), right-aligned when
// the base direction is RTL, with our own mouse hit-testing and horizontal scroll.
//
// Base direction lives in `dir` and is toggled with Ctrl+Left-Shift (LTR) /
// Ctrl+Right-Shift (RTL) while the field is focused (Windows convention).

#include <string>
#include "imgui.h"

namespace gt { enum class TextDir; } // model/Project.h

namespace gt::ui {

// Draw the note field at the current cursor, `width` px wide. Returns true if the
// note text or its direction changed this frame (caller should mark dirty/touched).
// `compact` hides the dir-radio row + resolved-direction indicator (used for short
// inline fields like sub-question labels); the clear button and Ctrl+Shift direction
// toggles still work either way.
bool bidiNoteInput(const char* id, std::string& text, gt::TextDir& dir, float width, bool compact = false);

} // namespace gt::ui
