# BodeX

A private, single-user Windows desktop app for tracking exercise/exam grading — a
students × questions grid modeled on the paper notebook a grader fills in by
hand. Written in C++ with [Dear ImGui](https://github.com/ocornut/imgui)
(Win32 + Direct3D 11), it builds to one self-contained `.exe` with the MinGW
toolchain and needs no external libraries installed.

![grid](docs/screenshot.png)

## What it does

- **Launch screen** — start a new project or resume a recent one.
- **New project setup** — choose the table size (N students × M questions).
  Students are just IDs `1..N`. Each question gets:
  - a point value,
  - a number of sub-questions,
  - an **Equal** split (each sub-question worth `points / count`) or a **Custom**
    split (type each sub-question's value, with a live sum-vs-total check).
- **Grading grid** — one cell per student × question. Per cell you record:
  - **awarded points** (typed directly),
  - **sub-questions answered** as `X / Y` — every cell starts assuming *all*
    answered; mark the skipped ones and their points are **locked out** (deducted
    from the awarded score and from the awardable max),
  - a **green full-marks tick** — all sub-questions correct ⇒ the cell
    automatically scores the question's full points,
  - a **last page** note (e.g. `p.14`) so you can resume a question-by-question
    pass where you left off.
- **No submission** — click a student's ID cell and toggle *No submission*; the
  whole row automatically scores 0 and is greyed out.
- **Live totals & stats** — each student's `score / max` updates as you grade, plus
  a class summary in the toolbar: student / graded / submitted counts, **two
  averages** — over everyone *and* over just the students who submitted (so the
  people who didn't hand in don't drag the class mean down) — and min / max. A
  **Stats** button opens a per-question breakdown (average, average %, and
  sub-questions answered among the students who attempted it) that flags the hardest
  question.
- **Undo / redo** — every grading action (cell edits, full-marks, paint, no
  submission) can be undone with **Ctrl+Z** and redone with **Ctrl+Y**; the grid
  jumps to the cell that changed so you can see what was reverted.
- **Question images** — click a question's column header to attach screenshots:
  the **question sheet** and **solution references** used to verify checks. Tag
  each image with the sub-question(s) it covers, and open previews in windows you
  keep beside the grid — several at once. **Scroll to flip** through a question's
  images, **Ctrl+scroll / `+` `-`** to zoom, **drag** to pan. Images are copied
  into an assets folder beside the project so they travel with it.
- **Saved as JSON** — one `.json` file per project; reopen it any time. Recent
  projects are remembered.
- **Autosave & crash recovery** — unsaved edits are quietly autosaved every ~30s; if
  BodeX ever closes unexpectedly, the next launch offers to recover the work.

Scoring precedence per cell: *no-submission* (row → 0) → *green tick* (→ full
points) → *awarded* (clamped to `[0, effective max]`, where the effective max is
the question's points minus any locked-out skipped sub-questions; values over it
are flagged in orange but never silently capped in storage).

## Requirements

- Windows 10/11.
- **MSYS2 / MinGW-w64** with `g++`, `mingw32-make`, and `windres` on `PATH`
  (this project was built with g++ 15.2). No CMake, Qt or vcpkg required.

The Direct3D 11 / DXGI / d3dcompiler libraries it links against ship with
Windows, and the binary is statically linked, so the resulting `.exe` runs
standalone.

## Build & run

```sh
mingw32-make          # build build/BodeX.exe (icon embedded)
mingw32-make run      # build and launch
mingw32-make test     # build & run the non-GUI core tests
mingw32-make clean    # remove build/
```

Then double-click `build/BodeX.exe` (or `mingw32-make run`). A **BodeX desktop
shortcut** (with the app icon) is also created for one-click access; recreate it
any time with:

```powershell
powershell -File tools/create_shortcut.ps1
```

### Quick look without setting up a project

Set `BODEX_DEMO=1` to launch straight into a populated sample grid (`=2` also
opens the cell editor). The demo project is in-memory only and is never written
to disk.

```sh
BODEX_DEMO=1 ./build/BodeX.exe
```

## Using it

1. **New Project** → set students, questions, and each question's points /
   sub-questions / split. A live **Total exam points** counter updates as you
   type. → **Create Project**.
2. **Right-click a cell** to toggle **full marks** (turns green); **hold and
   right-drag** to paint full marks across a whole **row or column** (direction
   picked from your drag). A cell that earns **full marks any way** — the tick, or a
   score typed/stepped up to the max — shows green **FULL**. **Left-click a cell** to
   open its editor: awarded points (with **`-`/`+`** buttons that dock from full),
   sub-questions answered, last page (a page number with `-`/`+` steppers), and a
   note. Sub-questions start all-answered; lower the count (equal split) or untick a
   part (custom split) and its points lock out of the awardable max. The **first**
   such change on a fresh cell awards the remaining full marks (the still-ticked parts
   are assumed correct — e.g. 4/4 → 3/4 on a 20-pt question gives 15/15); adjust from
   there and later changes deduct/add per part. The last page shows as `lp: N` on the
   cell's second line.
3. **Grade from the keyboard.** The selected cell has a blue outline. **Arrow
   keys** move the selection; **type a number** to set awarded points inline, or
   **`+`/`-`** to step the score by one in a single press — **`-`** docks a point from
   full (a blank cell jumps straight to full-minus-one) and **`+`** builds up from 0;
   step back up to the max and the cell reads green **FULL** again. Press **Space** to
   open the inline editor without typing (nothing changes until you edit a field);
   press **Space again** (or **`p`** straight from the grid) to add/change the **last
   page** — handy for tagging a page on a green FULL cell without disturbing its score.
   Inside the inline editor, **`f`** marks the cell **FULL** and hops straight into the
   last-page field, so *full + a page* is `Space → f → number → Enter`. **Enter**
   commits and moves down, **Tab** commits and moves right, **Esc** cancels. **`f`**
   (on the grid) toggles full marks, **`n`** toggles *No submission* for the row,
   **Del** clears the cell, **`e`** or **F2** opens the full cell editor, and **F1**
   (or **Help → Keyboard Shortcuts**) shows a shortcuts cheat-sheet. Clicking a cell
   also moves the selection there, so mouse and keyboard mix freely.
4. Click a **student ID** to mark *No submission* (row scores 0). No-submission rows
   are excluded from the **submitted** average, so a handful of no-shows won't sink
   the class mean — the toolbar shows both `avg` (everyone) and `avg(sub)` (submitters
   only). The **Stats** button opens the full breakdown, including per-question
   averages and the hardest question.
5. **Undo / redo.** **Ctrl+Z** undoes the last grading action, **Ctrl+Y** (or
   **Ctrl+Shift+Z**) redoes it — also on the **Edit** menu. A whole right-drag paint
   counts as one step, and the selection jumps to the reverted cell. History is
   per-session and resets when you start, open, or close a project.
6. **Manage the columns.** **Ctrl+scroll** over the grid zooms it in/out (handy on a
   second monitor). Finished with a question? **Fold** its column into a thin,
   **locked** strip that still shows a compact score (`F` / the points / `-`) but can't
   be changed by a stray click, keypress, or paint — so graded work is protected.
   **Right-click a column header** for the menu: *Size column to fit* / *Size all
   columns to fit*, *Fold* / *Unfold*. **Ctrl+click** (or **Shift+click** for a range)
   headers to select several, then *Fold selected*. Click a folded header to unfold it.
   Folds and column widths are saved with the project.
7. **Ctrl+S** (or the Save button) writes the project to a `.json` file. Closing
   with unsaved changes prompts to Save / Discard / Cancel. In between saves, unsaved
   work is **autosaved** every ~30s (and when you switch away or quit); if BodeX
   closes unexpectedly, the next launch offers to **recover** it. Autosave is a safety
   net only — your `.json` is still written by Save.

**Notes can be Hebrew (right-to-left).** The note field is bidirectional: Hebrew you
type flows right-to-left and English left-to-right, the field right-aligns for Hebrew,
and brackets/parentheses face the right way in RTL — what you type is exactly what is
stored. Direction is automatic (from the first letter); **Ctrl+Left-Shift** forces
left-to-right and **Ctrl+Right-Shift** forces right-to-left (the usual Windows
convention), and an `Auto / LTR / RTL` control sits under the field. The note reads
the same way in the grid's hover tooltip.

**Left-click** a **question's column header** to open its image menu — add / preview /
remove the question sheet and solution-reference screenshots, each tagged to the
sub-questions it covers. (Right-click the header for the column fold / size menu — see
step 6.) Each **Preview** opens a separate, resizable window, and
you can keep several open beside the grid while you grade. Inside a preview:
**mouse wheel** (or `< Prev` / `Next >`, or the arrow keys) flips through all of
that question's images; **Ctrl+wheel** or the `+` / `-` buttons/keys zoom;
**left-drag** pans a zoomed image; **`F`** fits, **`Esc`** closes.

Projects and the recent-projects list live under
`%APPDATA%\BodeX\` by default (you can Save As / Open anywhere). A question's
images are copied next to its `.json` in a `<project>.assets/` folder.

## Project file format

A project is a single JSON document — `schemaVersion` (currently `2`), `name`,
`createdIso`, the `questions` array (title, `maxPoints`, `subCount`, `split`,
`subPoints`, plus the column view state `folded` / `viewWidth`), and the `students`
array (`id`, `noSubmission`, and a `cells` array with `fullTick`, `awarded`,
`subAnswered`, `subChecks`, `lastPage`, `note`). It is plain text and easy to inspect
or diff. Older files open fine (missing keys take their defaults; v1 scores are
preserved on load).

## Layout

```
src/
  main.cpp                 Win32 + DX11 host and frame loop
  app/App.*                screen state machine, menu, save/exit guard
  ui/                      HomeScreen, NewProjectScreen, GradingTable,
                           CellEditor, widgets, native file dialogs
  model/                   Project (data), Scoring, Serialization (JSON), AppConfig,
                           Bidi (Hebrew/RTL note reordering)
  util/utf.h               UTF-8 <-> UTF-16 helpers
tests/test_core.cpp        scoring rules + JSON/file round-trip (make test)
third_party/               Dear ImGui + backends, nlohmann/json (vendored)
resources/                 app manifest (DPI awareness)
```

The build emits clang flags via `compile_flags.txt` so the clangd language
server resolves includes without extra configuration.

## License & credits

BodeX is released under the [MIT License](LICENSE).

It vendors two excellent MIT-licensed libraries (their license notices travel
with their source under `third_party/`):

- [Dear ImGui](https://github.com/ocornut/imgui) © Omar Cornut — immediate-mode GUI
- [nlohmann/json](https://github.com/nlohmann/json) © Niels Lohmann — JSON for Modern C++
- [stb_image](https://github.com/nothings/stb) by Sean Barrett — public-domain image loader (`third_party/stb/`)
