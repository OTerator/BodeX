#pragma once

// Small shared ImGui helpers used across screens.

#include <string>
#include "imgui.h"

namespace gt::ui {

// Format a double compactly: up to 2 decimals with trailing zeros trimmed
// ("12", "12.5", "7.25").
std::string fmtNum(double v);

// A checkbox styled green to read as a "full marks" tick. Returns true when the
// value was toggled this frame.
bool greenTickCheckbox(const char* label, bool* v);

// Draw text using the current font family at a larger pixel size.
void bigTitle(const char* text, float px = 26.0f);

} // namespace gt::ui
