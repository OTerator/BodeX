#pragma once

class App;

namespace gt::ui {

// Popups driven from the grading table. Both are submitted every frame after
// the table is drawn; they only show when their popup id is open.
void cellEditorPopup(App& app);   // edit one student x question cell
void studentMenuPopup(App& app);  // per-student (row header) menu: no-submission

// Open a notes-viewer window for (student, question), or raise it if one is
// already open for that cell (dedup per cell; several different cells' windows can
// be open at once). Driven from the grid's note badge (GradingTable.cpp).
void openNotesWindow(App& app, int student, int question);

// Non-modal notes-viewer windows (several can stay open beside the grid). Draws
// every entry in app.notesWins and prunes the closed ones.
void notesViewerWindows(App& app);

} // namespace gt::ui
