#pragma once

class App;

namespace gt::ui {

// Popups driven from the grading table. Both are submitted every frame after
// the table is drawn; they only show when their popup id is open.
void cellEditorPopup(App& app);   // edit one student x question cell
void studentMenuPopup(App& app);  // per-student (row header) menu: no-submission

} // namespace gt::ui
