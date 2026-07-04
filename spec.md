# BodeX — Project Spec & Contributor Guide

> Orientation for future Claude Code sessions working in this directory. Read
> this before making changes. It captures intent, architecture, invariants, and
> the non-obvious gotchas that are easy to trip over. Keep it up to date when you
> change something structural.

---

## 1. What BodeX is

A **private, single-user Windows desktop app** for a grader to track exam
grading. It reproduces and automates a paper-notebook workflow: a grid of
**students (rows) × questions (columns)**. Students are just IDs `1..N` (no other
detail). For each question the grader configures points and sub-questions; for
each cell the grader records the awarded points, how many sub-questions were
answered (`X/Y`, reference only), an optional **full-marks tick**, an optional
**last page** (a resume marker), and a note. Per-student and class totals compute
live. Projects are saved as JSON.

It is intentionally small, local, and dependency-light. There is no server, no
account, no network. Design for one grader on one machine.

Public repo: **https://github.com/OTerator/BodeX** (MIT).

---

## 2. Environment & toolchain (this machine)

- **OS:** Windows 11, high-DPI display (typically 150% scaling).
- **Compiler:** MSYS2 MinGW-w64 `g++` (15.2) at `C:\msys64\mingw64\bin`, on PATH.
- **Make:** `mingw32-make` (on PATH). `windres` for resources. **No CMake.**
- `pacman` exists at `C:\msys64\usr\bin\pacman.exe` if a package is ever needed,
  but the goal is **zero installs** — everything third-party is vendored.
- clangd LSP is enabled; it reads `compile_flags.txt` at the repo root.

The build links against Windows-provided libraries (Direct3D 11 / DXGI /
d3dcompiler / commdlg / shell32 / …) and links the C++ runtime **statically**, so
the resulting `build/BodeX.exe` is self-contained and needs no MinGW DLLs.

---

## 3. Build, run, test

From the repo root:

```sh
mingw32-make          # -> build/BodeX.exe
mingw32-make run      # build and launch
mingw32-make test     # build & run the non-GUI core tests (build/test_core.exe)
mingw32-make clean    # remove build/
```

`Makefile` notes worth knowing:
- It sets `SHELL := C:/msys64/usr/bin/sh.exe` so recipe commands (`mkdir -p`,
  `rm -rf`) work regardless of the shell that launches make. MSYS2 is required.
- Objects go to `build/obj/...` mirroring source paths. Output `build/BodeX.exe`.
- Link flags: `-mwindows` (GUI subsystem, no console — with MinGW a plain
  `int main()` is still the entry) and `-static -static-libgcc -static-libstdc++`.
- `LDLIBS`: `-ld3d11 -ld3dcompiler -ldxgi -ldwmapi -lgdi32 -lcomdlg32 -limm32
  -lole32 -luuid -lshell32`. The MSVC-only `#pragma comment(lib)` in the ImGui
  backends is ignored by g++, hence these are listed explicitly.
- `windres --include-dir resources resources/app.rc` compiles the app manifest
  (DPI awareness) + the icon (`resources/BodeX.ico`, id 100) into the exe.
- The `test` target compiles only `tests/test_core.cpp` + `model/Scoring.cpp` +
  `model/Serialization.cpp` + `model/AppConfig.cpp` (no ImGui) and links
  `-lshell32`.

**Demo modes** (env var, in-memory only, never saved):
- `BODEX_DEMO=1` → launch straight into a populated sample grid.
- `BODEX_DEMO=2` → same, and open the cell editor on the first cell.
- `BODEX_DEMO=3` → jump to the New Project screen.
Handled in `App::App()` (`src/app/App.cpp`, `buildDemoProject()`).

`BODEX_OPEN=<path>` opens a specific project on launch (used for "open with" and
for GUI testing without clicking through the launcher).

**Desktop shortcut:** `tools/create_shortcut.ps1` creates/refreshes
`Desktop\BodeX.lnk` (icon from `resources/BodeX.ico`, target `build/BodeX.exe`,
triggers a shell icon-cache refresh).

---

## 4. Repository map

```
Makefile                 build (mingw32-make); SHELL=MSYS2 sh
compile_flags.txt        clang flags for clangd (mirrors CXXFLAGS includes/defines)
README.md                user-facing overview + screenshot
spec.md                  THIS FILE (contributor guide)
CLAUDE.md                short pointer to spec.md (auto-loaded by Claude Code)
NOTES.md                 parked/deferred ideas
LICENSE                  MIT
docs/screenshot.png      README image
resources/               app.rc, app.manifest (DPI), BodeX.ico, BodeX.png (icon source)
tools/create_shortcut.ps1
src/
  main.cpp               Win32 window + D3D11 device/swapchain + ImGui frame loop.
                         Owns the platform; delegates all UI to App::render().
  app/App.{h,cpp}        Top-level state machine: Home/NewProject/Grading screens,
                         menu bar, dirty tracking, save/exit unsaved-changes guard,
                         open/save/close actions, the "paint full marks" gesture state.
  ui/HomeScreen.*        Launcher: New / Open / recent projects.
  ui/NewProjectScreen.*  Table-size + per-question config; live total-points counter.
  ui/GradingTable.*      The grid: header, cells, totals, status bar, popups, and
                         the left-click/drag "paint full marks" gesture.
  ui/CellEditor.*        Cell editor popup + student (no-submission) menu popup.
  ui/widgets.*           fmtNum(), greenTickCheckbox(), bigTitle().
  ui/platform_dialogs.*  Native Win32 open/save file dialogs (commdlg).
  ui/QuestionImages.*    Column-header image menu (add/preview/remove) + preview window.
  ui/ImageStore.*        Loads images -> D3D11 textures for ImGui::Image (uses stb_image).
  model/Project.h        Data model (header-only) + builders/normalizers.
  model/Scoring.{h,cpp}  Score computation + class stats. GUI-free.
  model/Serialization.*  Project <-> JSON (nlohmann) + UTF-8 file I/O. GUI-free.
  model/AppConfig.*      %APPDATA%\BodeX paths, recent-projects config, nowIso().
  model/Assets.{h,cpp}   Question-image asset dirs + copy/migrate. GUI-free (Win32 file ops).
  util/utf.h             UTF-8 <-> UTF-16 helpers (header-only, inline).
tests/test_core.cpp      Non-GUI tests: scoring, JSON/file round-trip, recent alias.
third_party/
  imgui/                 Dear ImGui 1.92.9 (master) core + win32/dx11 backends +
                         misc/cpp/imgui_stdlib (std::string InputText). MIT.
  json/json.hpp          nlohmann/json single header (v3.11.3). MIT.
  stb/stb_image.h        image decoder for ImageStore (public domain).
```

Namespaces: model + UI live in `namespace gt` / `gt::ui`. `App` is a **global**
class (so `main.cpp` can say `App app;`). UI screen functions take `App&` and are
declared with a forward `class App;`.

---

## 5. Data model (`src/model/Project.h`)

Header-only, GUI-free, unit-testable. Core structs:

```cpp
enum class SplitMode { Equal, Custom };

struct Question {
    std::string title = "Q1";
    double      maxPoints = 10.0;
    int         subCount  = 1;
    SplitMode   split     = SplitMode::Equal;
    std::vector<double> subPoints; // size == subCount once normalized
};

struct Cell {
    bool              fullTick    = false; // green tick -> full question points
    double            awarded     = 0.0;   // manually entered points
    int               subAnswered = 0;     // answered count (defaults to subCount)
    std::vector<char> subChecks;           // Custom split: per-sub answered flags (1=answered)
    std::string       lastPage;            // resume marker (stored as text; edited as int)
    std::string       note;
    bool              touched     = false; // distinguishes blank from an explicit 0
};

struct Student { int id; bool noSubmission; std::vector<Cell> cells; };

struct Project {
    int schemaVersion = 2;
    std::string name, createdIso;
    std::vector<Question> questions;
    std::vector<Student>  students; // size N; each student's cells size == questions size
};
```

**Sub-questions default to all-answered** and *skipped* ones lock out their points
(they cap the score — see §6). `subAnswered` is the answered count and drives the
deduction for an **Equal** split; `subChecks` (a per-sub-question answered mask)
drives it for a **Custom** split and is empty for Equal.

Helpers (inline in the header — keep them header-only so the non-GUI test build
doesn't need extra .cpp linkage):
- `equalShare(q)` = points per sub-question under an equal split.
- `normalizeQuestion(q)` — clamp subCount ≥ 1; size `subPoints` (Equal re-derives
  all shares; Custom preserves values, pads/truncates to subCount).
- `blankCell(q)` — a fresh cell defaulting to **all sub-questions answered**
  (`subAnswered = subCount`; Custom `subChecks` all-1). Use it (not `Cell{}`) when
  creating/clearing cells so grading starts from full marks.
- `lockedSubPoints(q, c)` — points locked out by skipped sub-questions (Equal:
  `skipped * equalShare`; Custom: sum of the skipped `subPoints`).
- `ensureShape(p)` — make the grid rectangular: normalize questions, assign IDs
  `1..N`, resize rows, and keep each cell's `subAnswered`/`subChecks` consistent
  with its question. Called after every load.
- `makeProject(name, N, questions)` — build an N×M project of `blankCell`s.

---

## 6. Scoring rules & invariants (`src/model/Scoring.*`)

Per-cell effective points, **highest precedence first**:
1. `student.noSubmission` → the whole row scores **0**.
2. `cell.fullTick` → the cell scores the question's **full `maxPoints`**.
3. otherwise → `clampCell(awarded, effectiveMax(q, c))` = clamp to `[0, effectiveMax]`.

- `effectiveMax(q, c)` = `maxPoints − lockedSubPoints(q, c)`: **skipped sub-questions
  lock out their points**, lowering the awardable ceiling. Equal split: each skip =
  `maxPoints/subCount`; Custom: the specific skipped `subPoints`.
- `studentTotal(p, s)` = 0 if no-submission, else sum of `cellPoints` over questions.
- `projectMaxTotal(p)` = sum of `q.maxPoints` (the "/ max" denominator — the *full*
  max; locked points don't shrink it).
- `cellOverMax(q, c)` = grader typed more than the **effectiveMax** (visual orange
  warning only; storage is never silently capped).
- `setAnsweredCount` / `setSubAnswered` (Scoring.*) are the editor helpers that
  lock/unlock sub-questions: locking deducts the sub-question's points from
  `awarded` (so 8/10 → 5.5/7.5 when one of four equal sub-qs is skipped), unlocking
  adds them back, then clamps `awarded` into `[0, effectiveMax]`. GUI-free/tested.
- `classStats(p)` = students / graded (no-submission or any touched-or-full cell) /
  average / min / max.

**Invariants to respect when editing:**
- **Sub-questions default to all-answered** (`blankCell`), and skipped ones now
  **cap the score** via `effectiveMax` — `subAnswered`/`subChecks` are *no longer*
  reference-only (they were pre-v2). `projectMaxTotal` stays the full max; the cell
  still displays `awarded` out of the question's full points externally.
- **Toggling / painting full marks must NOT set `cell.touched`.** A full cell
  counts as graded via `fullTick` regardless of `touched`; leaving `touched`
  alone means un-fulling a previously-blank cell returns it to blank `-` instead
  of a stray `0 0/Y`. See `GradingTable.cpp` (`applyFullRange`, the click toggle).
- The grid must stay rectangular — always go through `ensureShape` after loading
  or mutating structure.

---

## 7. Persistence (`Serialization.*`, `AppConfig.*`)

- **One JSON file per project.** `toJsonString` / `projectFromJsonString` (manual
  nlohmann mapping; `split` serialized as `"equal"`/`"custom"`). `saveProject` /
  `loadProject` do UTF-8 file I/O via `_wfopen` on the wide path (so Unicode paths
  work — libstdc++ fstream doesn't take wide paths on MinGW). `loadProject` runs
  `ensureShape` on the result.
- **`schemaVersion`** is stored. It is **`2`**. `projectFromJsonString` migrates
  `< 2` files: because v1 treated `X/Y` as reference-only, it resets every cell to
  all-answered (`subAnswered = subCount`, `subChecks` cleared) so no old grade is
  retro-deducted — **scores are preserved**, the stale reference counts are dropped.
  Bump `schemaVersion` and add a migration branch here for any future breaking change.
- Config: `%APPDATA%\BodeX\config.json` holds the recent-projects list;
  `%APPDATA%\BodeX\projects\` is the suggested (not enforced) save dir.
  `AppConfig` resolves these via `SHGetFolderPathW(CSIDL_APPDATA)` and creates
  them on demand. `loadConfig` filters out recents whose file no longer exists.
- **`addRecentProject` copies its argument first** — do not remove that copy. The
  Home screen used to pass a reference *into* `config.recentProjects`, and the
  erase/insert invalidated it (use-after-free crash on opening a recent). The Home
  screen now also copies + defers the open until after its loop. Regression test:
  `testRecentAliasSafe`.

---

## 8. App architecture & UI flow (`src/app/App.*`)

`main.cpp` runs the Win32 + DX11 host and calls `app.render()` once per frame
inside an ImGui frame; it checks `app.wantsQuit()` to leave. The window's close
box (`WM_CLOSE`) is routed through `App::requestQuit()` so the unsaved-changes
guard also covers the X button.

`App` is a simple screen state machine: `Screen { Home, NewProject, Grading }`.
`render()` handles global shortcuts (Ctrl+S/N/O), draws the main menu bar, then
the current screen, then the unsaved-changes modal. It also **toggles
`ImGuiConfigFlags_NavEnableKeyboard`** per frame: OFF on the bare grading grid (so
its keyboard-first grading owns the arrows/Tab — see §9), ON for popups and the
Home/NewProject screens.

Actions: `newProjectStart`, `createProjectFromDraft`, `openProjectDialog`,
`openProjectPath`, `doSave` (falls back to `doSaveAs` when no path), `doSaveAs`,
`closeProject`, `requestQuit`. The **unsaved-changes guard** (`guard(Pending, …)`
→ `renderUnsavedPrompt` → `performPending`) offers Save / Discard / Cancel before
any action that would lose work. `guard` only records the pending action and sets
`openGuardPopup_`; the actual `ImGui::OpenPopup` fires inside `renderUnsavedPrompt`,
**not** at the call site. This matters: a `guard` invoked from a File-menu item
runs under the menu's ID stack, and an `OpenPopup` there would hash to a different
id than the root-level `BeginPopupModal` and the modal would silently never appear
(so File → New/Open/Close/Exit would no-op when dirty). Deferring the open to the
same call as the modal keeps the ids matched for every caller (menu, Ctrl+N/O, and
the `WM_CLOSE` X button).

`NewProjectDraft` holds the New Project screen's editable state; `syncQuestions()`
keeps its `questions` vector sized to `questionCount` each frame.

Screens are **immediate-mode free functions** in `gt::ui` that read/mutate `App`.
Each fills the main viewport work area (below the menu bar). Popups
(cell editor, student menu, unsaved prompt) are opened via `OpenPopup` and drawn
after their trigger.

---

## 9. Grading grid interactions (important detail)

In `GradingTable.cpp`:
- **Right-click a cell** = toggle full marks. **Right-click + drag** = "paint" full
  marks across a **row or column** (axis locked from the initial drag direction).
- **Left-click a cell** = open the detailed cell editor (also moves the keyboard
  selection there).
- **Click a student ID** = student menu (No submission toggle).
- **Keyboard-first grading** (`handleGridKeyboard`): a selected "active" cell
  (`App::activeRow/activeCol`, blue outline) driven from the keyboard — **arrows**
  move it; **digits/`.`/`-`** begin an inline awarded-points edit; **Enter**/**Tab**
  commit and advance (down / right; Shift+Tab left); **Esc** cancels; **`f`**/**`n`**
  toggle full-marks / the row's no-submission; **Del**/**Backspace** clear the cell;
  **F2** opens the full editor. Mechanics detailed below.

  (L/R were swapped deliberately: the left-click editor uses `IsItemClicked`, which
  is occlusion-aware, so it won't fire for cells hidden under a floating image
  preview. The paint gesture below is raw-mouse + rect hit-test and is NOT itself
  occlusion-aware, so it is now **gated** on the grading window being hovered — see
  the note in `handlePaintGesture` below. The L/R swap is kept regardless, per the
  author's preference.)

The paint gesture is resolved centrally in `handlePaintGesture(app)` **after** the
table is drawn, using **recorded cell rectangles** (`g_cellRects`), NOT
`IsItemHovered()`. Why: while a cell button is held down it captures hover, so per-
cell `IsItemHovered()` returns false for the other cells you drag over. Each frame
every drawn (non-no-submission) grade cell pushes its screen rect + (row,col) into
`g_cellRects` (cleared at the top of `gradingScreen`); `cellUnderMouse()` hit-tests
the mouse against them, which works during the drag. `applyFullRange` fills all
cells from the anchor to the hovered cell along the locked axis, only ever
**setting** `fullTick = true` (never unsetting mid-drag), and skips no-submission
students. Gesture state lives on `App` (`paintActive`, `paintIsDrag`,
`paintAnchorRow/Col`, `paintAxis`). A plain click (no drag) toggles the anchor.
`handlePaintGesture` early-outs when a popup is open **or** when the mouse is not
over the grading window (`IsWindowHovered(ImGuiHoveredFlags_ChildWindows | …)`),
so dragging a floating preview across the grid never paints the occluded cells.

Cell rendering: each grade cell is a two-line `Button` (`##cell_i_j` id) sized to
two text lines. Line 1 = score + `X/Y` (or `FULL <pts>  Y/Y` for a full tick);
line 2 = `lp: <lastPage>`. Colors: green for full-tick, orange (`kOverBtn`) when
`cellOverMax`. No-submission rows are greyed via `TableSetBgColor` and their cells
disabled. The table uses `ScrollX|ScrollY|Resizable|SizingFixedFit`, frozen header
row + ID column (`TableSetupScrollFreeze(1,1)`), and an `ImGuiListClipper` for
rows.

**Keyboard-first grading (the non-obvious parts).** `handleGridKeyboard(app)` runs
near the top of `gradingScreen`, **before** `BeginTable`, so the selection / scroll
/ inline-edit take effect the same frame.

- **ImGui nav is turned OFF on the bare grading grid** (`App::render`, see §8): the
  grid is a table of `Button` widgets, so ImGui's built-in keyboard nav would
  otherwise fight the custom arrows/Tab (steal Tab, draw a focus rect, move focus).
  It's re-enabled when a popup is open or on the Home/NewProject screens.
- **Ownership gate:** `handleGridKeyboard` early-returns if a popup is open **or**
  `App::anyPreviewFocused` is set (a focused image-preview window handles its own
  keys — that flag is reset/set in `imagePreviewWindows`, `QuestionImages.cpp`).
  When the app isn't the OS-foreground window it gets no key events at all, so
  nothing fires regardless — no `IsWindowFocused` check is needed (and it would be
  wrong: the background grid window isn't ImGui-focused until you click it).
- **Force-render the active row:** `clipper.IncludeItemByIndex(activeRow)` submits
  it even when scrolled off, so it can be outlined and scrolled to. `gridScrollToActive`
  triggers `SetScrollHereY`/`SetScrollHereX(0.5)` on the active cell (vertical is
  reliable; horizontal is best-effort — a fully clipped column registers no item rect).
- **Inline edit (`renderInlineEdit`)** replaces the active cell's button with an
  `InputText(CharsDecimal|EnterReturnsTrue|CallbackAlways)` seeded with the typed
  digit. Gotcha handled: `SetKeyboardFocusHere` **selects-all** the seed, so the
  next keystroke would replace the first digit ("7.5" → ".5"). `inlineEditCallback`
  collapses that initial selection **once** (guarded by `gridEditDeselect`, detected
  via a live `SelectionStart != SelectionEnd`), so later digits append. Commit does
  `awarded = strtod(buf)`, `fullTick = false`, `touched = true`; **Esc** discards.
- Toggling full marks via **`f`** leaves `touched` alone (the §6 invariant), same as
  the mouse paint. Active-cell state lives on `App` (`activeRow/Col`, `gridEditing`,
  `gridEditBuf`, `gridEditFocus`, `gridEditDeselect`, `gridScrollToActive`) and is
  reset in `applyLoadedProject`/`closeProject` (indices point into the old grid).

---

## 9b. Question images (screenshots / solution references)

Per-question attachments, column-level (shared across students). Model:
`QuestionImage { file, role (Question|Solution), caption, subQuestions[] }` on
`Question::images` (`model/Project.h`); `Project::id` names the pre-save staging
dir. Accessed from the **column header** (click → `questionImagesPopup`), previewed
in **non-modal windows** (`imagePreviewWindows`) that stay open beside the grid.

- **Previews (`imagePreviewWindows`, `ui/QuestionImages.cpp`):** the "Preview"
  button in the popup opens (or raises) a floating window via `openPreview`; the
  window state is a `PreviewWin { id, question, image, zoom, fit, focusNext, open }`
  entry in `App::previews` (a vector — several can be open at once, e.g. a question
  beside its solution). Each window's title uses a stable `###bodex_img_preview_<id>`
  so its visible label (`i/N`, role) can change without ImGui re-creating it.
  `imagePreviewWindows` draws every open entry and prunes closed ones after the loop.
  In-window controls, so you never round-trip through the popup to change image:
  **mouse wheel flips** through *all* of that question's images (Question +
  Solution, wrap-around; `< Prev`/`Next >` buttons and Left/Right arrows do the
  same); **Ctrl+wheel, `+`/`-` buttons/keys** zoom (turns `fit` off); **left-drag
  pans** the zoomed image (via `SetScrollX/Y`); **`F`** toggles fit, **`Esc`**
  closes the focused window. Interaction is gated on the hovered/focused window so
  only one preview reacts. `App::previews` is cleared on `closeProject` **and**
  `applyLoadedProject` (its indices point into `project.questions`).
  `imagePreviewWindows` early-returns while `IsPopupOpen("Unsaved Changes")`, so no
  preview is drawn over the unsaved-changes modal — a focused floating window would
  otherwise render above it and swallow its Save/Discard/Cancel clicks, making the
  app impossible to close (the X button, Ctrl+N/O and File → Exit all route through
  that modal). Previews stay in `App::previews` and reappear once it closes.

- **Storage (copy-beside-project):** files live in `<project>.assets/` next to the
  `.json`; before first save they stage in `%APPDATA%\BodeX\staging\<id>\`.
  `App::assetsDir` is the current live dir. `Assets.{h,cpp}` provides
  `liveAssetsDir`, `projectAssetsDir`, `stagingAssetsDir`, `importImage` (copy in),
  and `syncImages` (migrate on Save / Save As). `doSave()` migrates staged files
  into `<project>.assets/`. `closeProject`/`applyLoadedProject` call
  `imageStoreReleaseAll()`.
- **Textures:** `ImageStore` (`ui/ImageStore.*`) decodes with stb_image
  (`STBI_WINDOWS_UTF8` for Unicode paths) into a D3D11 texture + SRV, cached by
  absolute path; `main.cpp` calls `imageStoreInit` after the DX11 backend and
  `imageStoreReleaseAll` before device cleanup. Draw with
  `ImGui::Image((ImTextureID)(intptr_t)srv, size)`.

## 10. ImGui 1.92 specifics & gotchas

- Vendored ImGui is **1.92.9 (master)** with matching win32 + dx11 backends and
  `misc/cpp/imgui_stdlib` (for `InputText(label, std::string*)`).
- Fonts: only the default font (ProggyClean) is loaded → **ASCII glyphs only**.
  Do **not** use non-ASCII glyphs (✓, —, •, …) in UI strings; they render as
  boxes. Use `FULL`, `-`, `|`, etc.
- Large text: `PushFont(nullptr, px)` / `PopFont()` (1.92 two-arg form) — see
  `bigTitle`. The single-arg `PushFont` no longer exists.
- **DPI:** the app manifest (`resources/app.manifest`) declares PerMonitorV2, and
  `main.cpp` scales the UI once via `ImGui_ImplWin32_GetDpiScaleForHwnd`,
  `style.ScaleAllSizes(dpi)`, and `style.FontScaleMain = dpi`. Without the
  manifest, Windows bitmap-scales (blurs) the window.
- `io.IniFilename = nullptr` (no imgui.ini written; app stays self-contained).
- Popups: only call `EndPopup`/`EndTable`/`EndChild` when the matching `Begin*`
  returned true where required; `Begin`/`End` (window) are always paired.
- **Custom table header rows must mirror `ImGui::TableHeadersRow()`**: wrap each
  `TableHeader()` in `PushID(column_n)`/`PopID()` and skip via the
  `TableSetColumnIndex()` return. `GradingTable.cpp` does this to make headers
  clickable (image menu). A version that omitted the per-column `PushID` corrupted
  the table's heap state, stomping the project's vector buffers and surfacing as an
  intermittent SIGSEGV in `classStats` (a hot function reading the corrupted data)
  — the classic "crash far from the cause" of heap corruption. If you see an
  intermittent crash that vanishes at `-O0`/under gdb, suspect heap corruption; a
  quick way to a backtrace is `gdb -batch -ex run -ex bt` on the `-O2 -g` build
  (`mingw32-make OPT='-O2 -g'`).
- `main()` + `-mwindows`: MinGW keeps `int main()` as the entry under the GUI
  subsystem, so no `WinMain` is needed and no console appears.

---

## 11. How to make common changes

- **Add a per-cell field:** add it to `Cell` (`Project.h`) → map it in
  `cellToJson`/`cellFromJson` (`Serialization.cpp`) → edit it in `cellEditorPopup`
  (`CellEditor.cpp`) → optionally show it in `cellSummary` (`GradingTable.cpp`) →
  cover it in `tests/test_core.cpp` (round-trip).
- **Add a per-question config field:** add to `Question` → `questionToJson`/
  `questionFromJson` → `NewProjectScreen.cpp` config row → `normalizeQuestion` if
  it needs derived state → tests.
- **Change scoring:** edit `cellPoints`/`studentTotal`/`effectiveMax` in
  `Scoring.cpp` and update `tests/test_core.cpp`. Remember the precedence order
  (noSubmission > fullTick > `clamp(awarded, effectiveMax)`) and that skipped
  sub-questions lock out points via `lockedSubPoints`.
- **Add a screen:** create `ui/<Name>.{h,cpp}` (function taking `App&`, forward-
  declare `class App;`), add a `Screen` enum value, dispatch in `App::render`,
  wire navigation. The Makefile globs `src/ui/*.cpp` automatically.
- **Add a menu action:** add a `MenuItem` in `App::renderMenuBar`; if it can lose
  unsaved work, route through `guard(Pending::…)`.
- Breaking model change → bump `Project::schemaVersion` and handle old files in
  `projectFromJsonString`.
- **Question images:** metadata in `model/Project.h` (`QuestionImage`) +
  `Serialization.cpp`; file storage in `model/Assets.*`; GPU textures in
  `ui/ImageStore.*`; UI in `ui/QuestionImages.*` and the `GradingTable` header.
  Keep `App::assetsDir` and the `doSave()` asset-migration in sync if you change
  where files live.
- New third-party include dir → add to both `Makefile` `INCLUDES` and
  `compile_flags.txt` (keep them in sync for clangd).

---

## 12. Verifying changes (do this, don't just build)

- **Core logic:** `mingw32-make test` (55 checks: scoring rules, JSON string +
  on-disk round-trip, recent-alias regression). Add cases when you touch model,
  scoring, or serialization.
- **It builds:** `mingw32-make` with no warnings.
- **GUI, visually:** the app is a real Win32 window; verify by screenshot. Launch
  with a demo mode and capture with **PrintWindow** (not screen-copy — a background
  process can't reliably foreground the window). The **capturing** PowerShell
  process must call `SetProcessDpiAwarenessContext((IntPtr)-4)` before
  `GetWindowRect`/`PrintWindow`, or the 150%-scaled window comes back virtualized
  and blurry. Recipe: `Start-Process` the exe with `BODEX_DEMO=1/2/3`, sleep ~2s,
  get `MainWindowHandle`, size a `System.Drawing.Bitmap` to the window rect,
  `PrintWindow(h, hdc, 2)`, save PNG. To drive clicks/drags, `SetWindowPos` the
  window topmost at a known origin, use the alt-key + `SetForegroundWindow` trick,
  then `SetCursorPos` + `mouse_event`. (This session verified full-marks click,
  drag-paint, recent-open, and DPI crispness this way.)

---

## 13. Conventions

- C++20. Match the surrounding comment density and naming. Keep the **model layer
  free of ImGui/Win32** so it stays testable (utf.h and Serialization/AppConfig
  are the only model files that touch Win32, and only for file paths / appdata).
- Prefer editing the existing modules over adding new ones; screens are small
  immediate-mode functions.
- ASCII-only in UI strings (font limitation, see §10).
- All user-facing text/paths are UTF-8 in the model; convert at the Win32 boundary
  with `util/utf.h`.
- `build/` and `.claude/settings.local.json` are git-ignored; vendored
  `third_party/` **is** committed so clones build with no downloads.

---

## 14. Parked / deferred work (`NOTES.md`)

- **Grades export** — CSV first, then match an Excel format the user will supply.
  Intended as a `File → Export…` menu entry (per-student totals + per-question
  scores). **Waiting on the Excel format before implementing.**
- Compact-cell eliding for very long `lastPage` values.
- Possible: per-question column show/hide, roster import.

---

## 15. Repo / GitHub

- Public: `OTerator/BodeX` (MIT `LICENSE`, © 2026). Vendored ImGui + nlohmann/json
  are MIT; their notices ship under `third_party/` (`imgui/LICENSE.txt`, and the
  MIT header inside `json.hpp`).
- Default branch `main`. Follow the repo's commit conventions; **only commit/push
  when the user explicitly asks.**
```
