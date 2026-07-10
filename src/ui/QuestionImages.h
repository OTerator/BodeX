#pragma once

class App;

namespace gt::ui {

// Column-header image menu: list/add/remove a question's images and open previews.
// Submitted after the grading table is drawn (opened via app.requestOpenImageMenu).
void questionImagesPopup(App& app);

// Pull an image off the clipboard into the transient "Add image" form for question
// qi (mirrors picking a file). Returns true if an image was found; else sets
// app.statusMsg and returns false. Backs both the popup's "Paste" button and the
// grid-level Ctrl+V (which opens the menu on success).
bool beginPasteImage(App& app, int qi);

// Non-modal image preview windows (several can stay open beside the grid while
// grading). Draws every entry in app.previews and prunes the closed ones.
void imagePreviewWindows(App& app);

} // namespace gt::ui
