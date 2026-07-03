# BodeX — known issues & future ideas

Parked items to revisit (not scheduled). See `spec.md` for architecture.

## Known issues

- **Image-preview click-through to the grid.** While the floating image-preview
  window is open, dragging it with the mouse over the area where the grid sits
  underneath also drives the grid's cell gesture, marking cells. Root cause: the
  paint gesture (`handlePaintGesture` in `GradingTable.cpp`) reads the raw mouse
  state and hit-tests `g_cellRects` by position, so it fires for cells that are
  visually *occluded* by the preview window (it ignores z-order / which window is
  hovered). The per-cell editor open uses `IsItemClicked`, which is occlusion-aware,
  so it does not have this problem.
  - **Plaster applied (done):** swapped cell buttons — left-click now opens the
    editor (occlusion-aware), right-click / right-drag does full marks. Since the
    preview is dragged with the *left* button, this stops the accidental marking
    for the common case. it is demanded by the author that the implementation
    remains this way as it is significantly more comfortable.
  - **Proper fix (TODO):** gate `handlePaintGesture` on the grading window being
    the hovered/topmost window at the mouse — e.g. only run it when
    `ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)` for the grading window
    is true (and no popup is open). Then the gesture never fires under the preview
    or any floating window, and the L/R convention becomes a free choice again.

## Feature ideas (roadmap)

**Close the loop & speed**
- **Grades export** — CSV first, then match the Excel/Moodle format the user will
  provide (per-student total + per-question breakdown). Planned as `File → Export…`.
  *Do not implement until the format is given.*
- **Question-by-question focus mode** — grade one question for all students, then
  the next, with next/prev-student navigation (matches how the user grades; the
  "last page" markers exist for exactly this).
- **Keyboard-first grading + inline quick-entry** — arrow keys to move between
  cells, type a number to set points, keys for full/no-submission, Enter/Tab to
  advance; click-to-type inline instead of always opening the popup.
- **Tick sub-questions to auto-compute the score** — the model already stores
  per-sub-question `subPoints`; let the grader check which sub-parts are correct
  and auto-sum `awarded` (+ fill `X/Y`).

**Data safety**
- **Autosave + crash recovery** — periodic autosave to `<project>.autosave`
  (already git-ignored) and a restore prompt on next launch.
- **Undo / redo** — an action stack for cell edits, full-ticks, paint, no-submission.

**Analytics (data already collected)**
- **Per-question stats** — average per question, hardest question, and
  sub-question answer rates from the `X/Y` values already recorded.
- **Grade distribution histogram.**

**Images (extend the current feature)**
- **Paste screenshot from clipboard (Ctrl+V)** to attach without a file dialog.
- **Pinned / side-by-side previews** (question + its solution together).

**Identity & rubric**
- **Roster import (IDs → names)** from CSV, shown in the ID column and exports.
- **Reusable deduction/comment snippets (rubric)** applied per sub-question for
  consistency and speed.

**Smaller polish**
- Search / jump to a student ID.
- Per-question column show/hide & reorder.
- Compact-cell eliding for very long `lastPage` values (middle-elide / tooltip).
- Settings screen (font size / theme); per-question rubric notes at config time.
- CSV/Excel *import* of a roster.
