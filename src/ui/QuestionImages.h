#pragma once

class App;

namespace gt::ui {

// Column-header image menu: list/add/remove a question's images and open previews.
// Submitted after the grading table is drawn (opened via app.requestOpenImageMenu).
void questionImagesPopup(App& app);

// Non-modal image preview window (can stay open beside the grid while grading).
void imagePreviewWindow(App& app);

} // namespace gt::ui
