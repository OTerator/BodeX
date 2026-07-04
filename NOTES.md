# BodeX — known issues & future ideas

Parked items to revisit (not scheduled). See `spec.md` for architecture.

## Known issues

- *(none currently — see "Resolved" below)*

## Resolved

- **Can't quit while a preview is open (fixed).** With an image preview open,
  clicking the X (or Ctrl+N/O, File → Exit) popped the *Unsaved Changes* modal, but
  the focused preview window rendered *above* it and swallowed the
  Save/Discard/Cancel clicks — the app could not be closed. **Fix:**
  `imagePreviewWindows` early-returns while `IsPopupOpen("Unsaved Changes")`, so
  previews are suppressed while the modal is up and reappear on Cancel. Sibling to
  the preview-over-**grid** paint fix below.
- **Menu-invoked guard never showed the modal when dirty (fixed).** Found while
  verifying the above. `guard()` called `ImGui::OpenPopup` at its call site; from a
  File-menu item that runs under the menu's ID stack, so the popup id never matched
  the root-level `BeginPopupModal` and the modal silently never appeared — File →
  New/Open/Close/Exit just no-oped on a dirty project. **Fix:** `guard` now sets
  `openGuardPopup_` and the `OpenPopup` is deferred into `renderUnsavedPrompt`
  (same id stack as the modal), so every caller — menu, Ctrl+N/O, and the X button
  — resolves correctly.
- **Image-preview click-through to the grid (fixed).** Dragging a floating preview
  over the grid used to drive the cell paint gesture, marking occluded cells,
  because `handlePaintGesture` (`GradingTable.cpp`) reads raw mouse state and
  hit-tests `g_cellRects` by position, blind to z-order. **Proper fix applied:**
  `handlePaintGesture` now early-outs unless the grading window is hovered
  (`IsWindowHovered(ImGuiHoveredFlags_ChildWindows | AllowWhenBlockedByActiveItem)`),
  so the gesture never fires under a preview or any floating window.
  - The earlier plaster — left-click opens the editor (occlusion-aware),
    right-click / right-drag does full marks — is **kept**: the author finds L=edit
    / R=full significantly more comfortable, independent of the occlusion fix.

## Feature ideas (roadmap)

**Close the loop & speed**
- **Grades export** — CSV first, then match the Excel/Moodle format the user will
  provide (per-student total + per-question breakdown). Planned as `File → Export…`.
  *Do not implement until the format is given.*
- **Question-by-question focus mode** — grade one question for all students, then
  the next, with next/prev-student navigation (matches how the user grades; the
  "last page" markers exist for exactly this).
- ~~**Keyboard-first grading + inline quick-entry**~~ — **done:** a selected cell
  (blue outline) moves with the arrow keys; type a number for inline awarded-points
  entry (Enter/Tab commit + advance, Esc cancels); `f` full marks, `n`
  no-submission, `Del` clears, `F2` opens the full editor. See spec.md §9.
  - **`+`/`-` point stepping** (done): standing on a cell, `+`/`-` steps awarded ±1
    in place, and the cell editor's `-`/`+` buttons do the same — both mirror the
    sub-question sync, so the first `-` on a blank/full cell assumes full marks and
    docks from there (`+` builds up from 0). `gt::stepAwarded`, spec.md §6/§9.
- ~~**Tick sub-questions to auto-compute the score**~~ — **done** (v2 + sync): cells
  default to all-answered and skipped sub-questions **lock out** their points (equal
  split by count; custom split by per-sub-question ticks, deducting each part's
  `subPoints`). The **first** tick/count interaction on a blank or full cell now
  *syncs* `awarded` to the effective max (the still-ticked parts are assumed correct),
  so a fresh 20-pt / 4-sub cell dropped to 3/4 reads 15/15 instead of 0; later edits
  deduct/add per share. This effectively covers the "check the correct parts" case;
  a dedicated pure auto-*sum*-from-scratch mode is no longer needed.

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
- ~~Side-by-side previews (question + its solution together)~~ — **done:** previews
  are multiple non-modal windows now (`App::previews`); open both and place them
  side by side.
- **Pinned preview** — a preview that snaps to a fixed, non-overlapping strip on
  the right of the grid (still open; see the deferred docking note at the bottom).

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

## Deferred: dock/pin a preview next to the Total column

Considered during the preview UX overhaul and **deferred** in favour of the
multiple free-floating windows (`App::previews`) + wheel-to-flip flow, which
covers the same need more cheaply. Revisit only if that flow proves insufficient.

The user's original idea was to drag a preview into the header row so it "connects"
to the right of the Total column — i.e. a docked pane that never overlaps the grid.
Two ways to build it if picked up:
- **Custom right-side split (no new deps):** reserve a fixed-width strip on the
  right of the grading viewport, shrink the grid's `Begin`/table region to the
  remaining width, and draw the active preview there each frame (non-movable).
  Fits the current non-docking ImGui and the `io.IniFilename=nullptr` invariant.
- **Real ImGui docking:** swap the vendored ImGui to the *docking* branch and add
  a dockspace. More powerful (drag-to-dock, tabs) but touches the build, the
  backends, and the self-contained/no-ini invariant — the heavier option.
