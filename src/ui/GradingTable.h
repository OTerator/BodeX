#pragma once

class App;

namespace gt::ui {

// Main grading view: header/toolbar, the students x questions grid, live totals,
// the status bar, and the cell/student popups.
void gradingScreen(App& app);

// The grid keyboard-shortcut legend, as one plain-text block. Shared source for
// the F1/Help overlay (App::renderShortcutsOverlay) so the list lives in one place.
const char* gridShortcutsText();

} // namespace gt::ui
