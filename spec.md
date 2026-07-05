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
- **Header-dependency tracking (`-MMD -MP`).** The object rule compiles with
  `$(DEPFLAGS) = -MMD -MP`, emitting a `build/obj/**/*.d` beside each `.o` that lists
  the (non-system) headers it includes; the Makefile `-include $(DEPS)`s them at the
  bottom. So editing a header now recompiles **every** `.cpp` that includes it (e.g.
  touching `App.h` rebuilds `main`, `App`, and the `ui/*` screens) — incremental
  builds are safe. *Why it matters:* before this, header edits recompiled nothing, so
  a struct-layout change (add/remove/reorder a member of `App`, `Cell`, …) left stale
  objects with the **old layout** → an ODR/layout mismatch → intermittent
  `0xC0000005` / `0xC0000374` **memory-corruption** crashes "far from the cause". A
  full `mingw32-make clean` is now only needed if you ever suspect the `.d` files are
  out of sync.

**Demo modes** (env var, in-memory only, never saved):
- `BODEX_DEMO=1` → launch straight into a populated sample grid.
- `BODEX_DEMO=2` → same, and open the cell editor on the first cell.
- `BODEX_DEMO=3` → jump to the New Project screen.
Handled in `App::App()` (`src/app/App.cpp`, `buildDemoProject()`). The demo cell's
note is a Hebrew string, to exercise the BiDi note field (§9c).

`BODEX_FOCUS_NOTE=1` focuses the note field when the cell editor opens (test aid only;
lets scripted keystrokes land in the note — default UX still focuses grading).

`BODEX_OPEN=<path>` opens a specific project on launch (used for "open with" and
for GUI testing without clicking through the launcher).

`BODEX_AUTOSAVE_SEC=<seconds>` overrides the 30 s autosave interval (§8c) — set it
low (e.g. `2`) to exercise autosave / crash-recovery quickly during GUI testing.

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
  ui/BidiInput.*         BiDi-aware WYSIWYG note field (Hebrew/RTL editing, §9c).
  ui/widgets.*           fmtNum(), greenTickCheckbox(), bigTitle(), the notes-font
                         push/pop + BiDi display helpers (noteVisual/noteIsRtl, §9c).
  ui/platform_dialogs.*  Native Win32 open/save file dialogs (commdlg).
  ui/QuestionImages.*    Column-header image menu (add/preview/remove) + preview window.
  ui/ImageStore.*        Loads images -> D3D11 textures for ImGui::Image (uses stb_image).
  model/Project.h        Data model (header-only) + builders/normalizers.
  model/Scoring.{h,cpp}  Score computation + class stats. GUI-free.
  model/Bidi.{h,cpp}     Reduced Unicode BiDi (logical<->visual) for Hebrew notes +
                         portable UTF-8<->codepoint helpers. GUI-free, unit-tested (§9c).
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
enum class TextDir  { Auto, LTR, RTL }; // note base direction (Hebrew/RTL, §9c)

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
    TextDir           noteDir     = TextDir::Auto; // note base direction (Hebrew/RTL, §9c)
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
- `firstGradingDiff(a, b)` — first `(row, col)` where two `students` vectors differ,
  scanning row-major; `{-1, -1}` if identical, `{row, 0}` for a row-level change
  (noSubmission / row length). Used by undo/redo (§8b) to jump the selection to the
  reverted cell.

`Cell` and `Student` have a **defaulted `operator==`** (C++20) — every field
participates. The undo history (§8b) uses it to tell whether a grading edit actually
changed the grid (so image-only edits don't fabricate a dead undo step).

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
- `isFullMarks(q, c)` = the cell **earns full marks** and should read green FULL:
  an explicit `fullTick`, **or** `awarded == maxPoints` with no sub-question locked
  (`effectiveMax == maxPoints`). Over-max (`awarded > maxPoints`) is *not* full — it
  stays the orange warning. This is a **display/behavior** predicate only; `cellPoints`
  already scores a typed full as full. It unifies the two looks a full cell used to
  have (green `fullTick` vs a blue typed `20 4/4`). Drives the grid's green style and
  `FULL` label (`renderGradeCell`/`cellSummary`). *Nuance:* once `awarded == maxPoints`
  a cell reads green regardless of the `fullTick`, so `f` can't "un-full" a typed-full
  cell — use `-`.
- `setAnsweredCount` / `setSubAnswered` (Scoring.*) are the editor helpers that
  lock/unlock sub-questions. **First interaction** on a cell that is still blank
  (`!touched`) or a full tick: the ticked sub-questions are assumed correct, so
  `awarded` is *synced* to `effectiveMax` for the new state (a blank cell's `awarded`
  starts at 0, so nudging it by one share would wrongly land at 0 — sync it up
  instead; this is why 4/4 → 3/4 on a fresh 20-pt cell now reads 15/15, not 0).
  **Afterwards:** locking deducts the sub-question's points from `awarded` (so 8/10 →
  5.5/7.5 when one of four equal sub-qs is skipped), unlocking adds them back, then
  clamps `awarded` into `[0, effectiveMax]`. Either way `fullTick` clears once a
  sub-question is skipped and the cell is marked `touched`. GUI-free/tested.
- `stepAwarded(q, c, delta)` (Scoring.*) steps `awarded` by `delta` (±1) in **one
  press**. Baseline: a green/full cell (`fullTick`) → the full `maxPoints`; a blank
  cell (`!touched`) → `effectiveMax` for `-` but `0` for `+` (so `-` docks from full,
  `+` builds up from 0); a graded cell (incl. a typed full) → its current `awarded`.
  Then `awarded = clamp(baseline + delta, 0, effectiveMax)`, `fullTick` cleared,
  `touched` set. So green FULL `-` → `19` in one press, and `+` back to the max reads
  green again via `isFullMarks` (not a re-set tick). Used by the cell editor's −/+
  buttons and the grid's `+`/`-` keys (§9). GUI-free/tested. *(Distinct from the
  sub-question helpers above, which keep the sync-then-deduct behavior.)*
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
  New scalar fields that default sensibly can be added **without** a bump: e.g.
  `Cell::noteDir` is serialized as an int (`0=Auto`) and older files that omit the
  key load as `Auto` — additive, so it stayed on schema 2.
- Config: `%APPDATA%\BodeX\config.json` holds the recent-projects list **and the
  pending-autosave record** (`AutosaveRecord`, §8c); `%APPDATA%\BodeX\projects\` is
  the suggested (not enforced) save dir and `%APPDATA%\BodeX\autosave\` holds the
  crash-recovery files. `AppConfig` resolves these via
  `SHGetFolderPathW(CSIDL_APPDATA)` and creates them on demand (`appDataDir`,
  `projectsDir`, `autosaveDir`). `loadConfig` filters out recents whose file no longer
  exists **and** drops the autosave record if its file is gone. The `config <-> JSON`
  mapping is factored into pure, unit-tested helpers `configToJsonString` /
  `configFromJsonString` (mirroring `Serialization`'s split); `save/loadConfig` layer
  file I/O over them. `removeFile` (Win32 `DeleteFileW`) is the model layer's file
  delete (used to clean up a spent autosave).
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
`render()` handles global shortcuts (Ctrl+S/N/O, and Ctrl+Z / Ctrl+Y undo-redo —
§8b), draws the main menu bar (File / **Edit** / Help), then the current screen,
then the unsaved-changes modal, and finally calls `maybeCommitUndo()` (§8b). It also
**toggles `ImGuiConfigFlags_NavEnableKeyboard`** per frame: OFF on the bare grading
grid (so its keyboard-first grading owns the arrows/Tab — see §9), ON for popups and
the Home/NewProject screens.

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

## 8b. Undo / redo history (`App.cpp`)

A **coalescing snapshot history** over the grading grid. A snapshot is a deep copy
of **`project.students` only** — the grading data. `Cell`/`Student` are pure value
types, so a copy of a realistic grid is tens of KB (sub-millisecond). State on `App`:
`undoStack_`, `redoStack_` (`vector<vector<gt::Student>>`), `undoBaseline_` (the
last-committed grading state), `undoPending_`, and `kUndoDepth` (100).

**Why students-only.** Questions are edited only *before* creation (New Project
screen); post-creation the sole mutable part of `project.questions` is `images`. So
"grading data" is exactly `project.students`. Snapshotting only that (1) keeps
question-image add/remove **out** of history — the confirmed scope — and (2) leaves
`App::previews` valid across a restore (they index into `questions[].images`, which
restore never touches), so open preview windows aren't disturbed.

**How it commits (no per-site instrumentation).** `markDirty()` — the one signal all
19 grading mutation sites already call (§6, §9) — now also sets `undoPending_`.
`maybeCommitUndo()` runs at the **end of every `render()`** and checkpoints only once
the action has *settled*: it early-returns while `paintActive || gridEditing ||
gridEditPageActive || ImGui::IsAnyItemActive()`. That gate collapses a multi-frame
action — a right-drag paint, a typed inline edit, or typing in a cell-editor field —
into a **single** history entry. On commit it pushes `undoBaseline_` to `undoStack_`
(capped at `kUndoDepth`, oldest dropped), clears `redoStack_`, and adopts the current
grid as the new baseline. The `project.students == undoBaseline_` guard (via the
model's defaulted `operator==`, §5) is what enforces grading-only: an image add/remove
sets `undoPending_` but leaves `students` unchanged, so **no** entry is created.

**Restore (`undo`/`redo`).** Move the current `students` onto the opposite stack, pop
the target snapshot into `project.students`, reseed `undoBaseline_`, set `dirty`, and
`abortInProgressEdit()` first (cancels any inline edit / paint gesture). The selection
jumps to `firstGradingDiff(before, after)` (§5) and `gridScrollToActive` is set, so the
grader sees what changed. `clampActive()` keeps the indices in range defensively.

**Shortcuts & menu.** `render()` fires `undo()`/`redo()` on Ctrl+Z / Ctrl+Y (and
Ctrl+Shift+Z), **gated** on `screen == Grading && !gridEditing && !anyPopup &&
!IsAnyItemActive()` so Ctrl+Z falls through to ImGui's built-in field text-undo while
editing. The **Edit** menu (`renderMenuBar`) has Undo/Redo items, greyed via
`canUndo()`/`canRedo()`. History is in-memory only and `resetHistory()` clears it on
every wholesale project swap (`createProjectFromDraft`, `applyLoadedProject`,
`closeProject`, the demo build).

*Extending it:* any **new** grading mutation is undoable for free as long as it calls
`markDirty()` and writes into `project.students`. A future edit that mutates
`project.questions` at grading time would need its own handling (or would ride along
only if the snapshot type widened to the whole `Project`).

---

## 8c. Autosave & crash recovery (`App.cpp`)

**Crash insurance, never a replacement for Save.** While a project has unsaved edits,
a full JSON copy is written to `%APPDATA%\BodeX\autosave\<project.id>.autosave`. The
autosave never touches the user's `.json` — an explicit Save is still the durable copy
(and, being durable, *deletes* the autosave). It also does not touch image assets:
files are addressed by id/path and copied only on real Save (§9b), so serializing the
project JSON is sufficient.

**Location — central appdata folder, keyed by `project.id`.** Both saved and
never-saved projects autosave to the same `autosaveDir()` file, named by the id (which
round-trips through JSON, so the path is **stable across Save / Save As**). This
mirrors the image `staging\<id>` dir (§9b), keeps the user's own folders free of stray
sidecar files, and makes launch-time discovery a single-folder concern. (An id-less
very-old file gets one assigned lazily in `autosaveTarget()`.)

**Cadence — 30 s interval, settled, plus two flushes.** `maybeAutosave()` runs at the
end of `render()` right after `maybeCommitUndo()` and shares its **settle gate**
(`!paintActive && !gridEditing && !gridEditPageActive && !IsAnyItemActive()`), so it
never writes mid-drag / mid-inline-edit; it is rate-limited by
`ImGui::GetTime() - lastAutosave_ >= autosaveInterval_` (default 30 s,
`BODEX_AUTOSAVE_SEC` overrides — §3). Per-change saving is deliberately avoided:
`saveProject` re-serializes the whole file synchronously on the UI thread, so
per-change writes would hitch the frame. Two **immediate** flushes capture the tail:
`flushAutosave()` on **focus-loss** (`WM_ACTIVATEAPP` FALSE → `g_appDeactivated` →
`app.flushAutosave()` in the `main.cpp` loop) and at the top of `requestQuit()` before
the close/quit guard.

**The config record is the crash marker.** `writeAutosave()` records
`config.autosave = { file, projectPath, name, savedIso }` (`AutosaveRecord`, §7) and
`saveConfig`s it. Every **clean** transition calls `clearAutosave()` — which deletes
the *recorded* file (so a Save-As move still cleans the right one) and clears the
record: successful `doSave()`, `closeProject()`, the start of `createProjectFromDraft`
/ `applyLoadedProject`, and the `Pending::Quit` path (covers Discard→Quit). So a record
that **survives to the next launch means the last session didn't exit cleanly**.

**Recovery prompt.** `App::App()` arms `openRestorePopup_` when it lands on Home with
no project and `config.autosave` still points at an existing file. `renderRestorePrompt()`
(a root-level `BeginPopupModal("Recover Unsaved Work")`, mirroring `renderUnsavedPrompt`'s
deferred-open id-stack trick, §8) offers **Restore** → `restoreFromAutosave()` (loads the
file, adopts it like `applyLoadedProject` but sets `projectPath` from the record — may be
`""` for a never-saved project — marks it `dirty`, and **keeps** the record so the next
tick overwrites the same file) or **Discard** → `clearAutosave()`. The BODEX_DEMO project
sets `demoMode_` and is never autosaved (so it can't fabricate a recovery prompt).

---

## 9. Grading grid interactions (important detail)

In `GradingTable.cpp`:
- **Right-click a cell** = toggle full marks. **Right-click + drag** = "paint" full
  marks across a **row or column** (axis locked from the initial drag direction).
- **Left-click a cell** = open the detailed cell editor (also moves the keyboard
  selection there).
- **Click a student ID** = student menu (No submission toggle).
- **Undo / redo** (Ctrl+Z / Ctrl+Y) are resolved in `App::render` (§8b), **not**
  here — gated so that while an inline edit is open Ctrl+Z stays the InputText's own
  text-undo rather than a document undo.
- **Keyboard-first grading** (`handleGridKeyboard`): a selected "active" cell
  (`App::activeRow/activeCol`, blue outline) driven from the keyboard — **arrows**
  move it; **digits/`.`** begin an inline awarded-points edit; **`+`/`-`** step the
  awarded points ±1 in place in one press (`-` docks from full/current, `+` builds up
  — `gt::stepAwarded`; `-` is no longer an inline-edit seed char); **Space** opens the
  inline editor in *review* mode (nothing typed) — press it **again** (or **`p`**
  straight from the grid) to step into the inline **last-page** field, so you can add a
  page to any cell (incl. a green FULL) without disturbing its score; **Enter**/**Tab**
  commit and advance (down / right; Shift+Tab left); **Esc** cancels; **`f`**/**`n`**
  toggle full-marks / the row's no-submission; **Del**/**Backspace** clear the cell;
  **`e`**/**F2** open the full editor; **F1** (or **Help → Keyboard Shortcuts**)
  toggles a shortcuts help overlay. Mechanics detailed below.

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
- **Inline edit (`renderInlineEdit`)** replaces the active cell's button with a score
  `InputText(CharsDecimal|EnterReturnsTrue|CallbackAlways)` (line 1). **Two ways in:**
  (a) typing a **digit/`.`** seeds the buffer with it and marks the score dirty
  (`gridEditScoreDirty`), collapsing `SetKeyboardFocusHere`'s **select-all** once (via
  `gridEditDeselect`/`inlineEditCallback`, detected by a live `SelectionStart !=
  SelectionEnd`) so later digits append; (b) **Space** opens it in *review* mode —
  seeded with the cell's current value (`fmtNum(maxPoints)` if `isFullMarks`, else
  `awarded`, else empty), `gridEditScoreDirty=false`, select-all **kept** so a first
  keystroke replaces the seed. `IsItemEdited()` flips `gridEditScoreDirty` on any real
  score change. **Space (again)** steps into a second `lp:` last-page `InputText` (line
  2) — `gridEditPageActive`/`gridEditPageBuf`/`gridEditPageFocus`, seeded with the
  current `lastPage`; a one-frame `gridEditSuppressSpace` swallows the very Space that
  *opened* the editor so it doesn't jump straight to the page. Commit (Enter → down,
  Tab → right) writes **only edited fields**: the score (`awarded = clamp(strtod(buf))`,
  `fullTick = false`, `touched = true`) **only if `gridEditScoreDirty`**, and the page
  (`lastPage`, **no** `touched` — a resume marker isn't a grade) **only if stepped
  into** and changed; `markDirty()` fires only if something changed. So Space→page→Enter
  on a green FULL cell keeps it FULL and just adds the page, and Space→Esc (or a
  no-edit commit) leaves the cell exactly as it was. State is reset in
  `applyLoadedProject`/`closeProject` alongside `gridEditing`. **`p`** from the grid is
  a shortcut for Space-then-Space: it opens the inline editor with `gridEditPageActive`
  already set and focus on the `lp:` field (score seeded but `gridEditFocus=false`), so
  tagging a page is one key.
- **`f` inside the inline editor** toggles `fullTick` (CharsDecimal filters the letter
  out of the score buffer, so it is a clean hotkey read with `IsKeyPressed`, like
  Space/Tab/Esc). It leaves `touched` alone (the §6 invariant); on turning FULL **on**
  it steps into the `lp:` field (reusing the Space-step), and it clears
  `gridEditScoreDirty` so the commit path preserves the tick — so `Space→f→<page>→Enter`
  marks the cell FULL and records the page in one flow. Pressing `f` again un-fulls
  (and, since a fresh cell stays `!touched`, un-fulling a blank cell returns it to `-`,
  not a stray `0 0/Y`). **Gotcha:** the handler must **not** write `gridEditBuf` — that
  string backs the *active* score `InputText`, and reseeding an active InputText desyncs
  ImGui's edit state (it writes its stale buffer back on the focus change to `lp:` and
  trips `IsItemEdited`, which would set `gridEditScoreDirty` and make the commit clear
  `fullTick`). Leave the buffer alone; the green FULL shows on commit.
- Toggling full marks via **`f`** (on the grid) leaves `touched` alone (the §6
  invariant), same as the mouse paint. **`e`** mirrors **F2** (open the cell editor).
  **F1** toggles `App::showShortcuts`. The overlay itself (`App::renderShortcutsOverlay`)
  and the F1 keypress are handled at **app level** in `App::render` — not in
  `gradingScreen` — so **Help → "Keyboard Shortcuts" (F1)** (a live checkbox bound to
  the same flag) and F1 both work on every screen. The legend text is one shared source,
  `gt::ui::gridShortcutsText()`; the window has no interactive widgets and uses
  `NoFocusOnAppearing`, so it doesn't fight the grid's key handling. Active-cell state lives on `App` (`activeRow/Col`,
  `gridEditing`, `gridEditBuf`, `gridEditFocus`, `gridEditDeselect`,
  `gridScrollToActive`) and is reset in `applyLoadedProject`/`closeProject` (indices
  point into the old grid).

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

## 9c. Hebrew / bidirectional (BiDi) cell notes

Cell **notes** are edited in a **BiDi-aware, WYSIWYG** field (`ui/BidiInput.*`):
Hebrew flows right-to-left, English left-to-right, the field right-aligns in RTL, and
paired punctuation mirrors correctly — all while typing. Two hard facts shape the
design: Dear ImGui has **no BiDi engine** (it draws/edits glyphs strictly left-to-
right in memory order and `InputText` has no RTL mode), and the default ProggyClean
font is ASCII-only. So the feature is these cooperating pieces:

- **Glyphs.** `main.cpp` keeps ProggyClean as the main UI font (`AddFontDefault`) and
  loads a scalable Hebrew-capable **system** font (Arial → Segoe UI → Tahoma, first
  that exists) as a *separate* font. `gt::ui::setNotesFont` registers it; the note
  tooltip and the CellEditor note field wrap their draw in
  `gt::ui::pushNotesFont()`/`popNotesFont()` (a null font — missing file — just keeps
  the current font, so it degrades to glyph-less without crashing). The rest of the
  UI is untouched, so the ASCII-chrome rule (§10/§13) still holds.
- **Reorder + mirror.** `model/Bidi.{h,cpp}` is a **reduced Unicode BiDi Algorithm**
  (UAX #9) sufficient for Hebrew (strong L/R + basic AL, European/Arabic numbers,
  neutrals; no explicit embeddings, no Arabic shaping). `bidiReorder(logical, base)`
  returns the visual codepoints, logical↔visual index maps, and a per-visual-glyph
  `visualRtl` flag; glyphs at RTL levels are **mirrored** (`bidiMirror`: `()[]{}<>«»`)
  in the visual copy only — so a typed `(` is *stored* as `(` and merely *drawn* as
  `)` in RTL. `bidiVisualUtf8` is the UTF-8 convenience wrapper. Newlines are hard
  separators. GUI-free and **unit-tested** (`testBidi` — pure Hebrew reverses, numbers
  keep LTR inside RTL, a mirrored-bracket case, `visualRtl` flags, etc.).
- **The editable widget.** `gt::ui::bidiNoteInput` wraps a real `ImGui::InputText`
  (single-line) so ImGui's mature engine does all editing on the logical UTF-8 buffer
  (insert, delete, selection, clipboard, IME, undo). It pushes `ImGuiCol_Text`,
  `ImGuiCol_InputTextCursor` and `ImGuiCol_TextSelectedBg` to **alpha 0** to hide
  ImGui's own rendering, uses `ImGuiInputTextFlags_CallbackAlways` to read/**write**
  the cursor (byte offsets), and repaints the text/caret/selection itself in visual
  order (notes font), right-aligned in RTL, with horizontal scroll. Mouse clicks
  hit-test the visual layout and override the cursor via the callback; plain Left/Right
  move the caret by **screen direction**. Polish: an `Auto/LTR/RTL` control + resolved-
  direction indicator, a placeholder, and an in-field clear (×). **Invariant:** the
  widget never writes the buffer except through ImGui's editor, so notes round-trip to
  disk exactly as typed. `Cell::noteDir` (`TextDir{Auto,LTR,RTL}`, §5/§7) is the base-
  direction override, toggled with **Ctrl+Left-Shift / Ctrl+Right-Shift** while focused
  (`Auto` = first strong char). The grid hover **tooltip** reuses `gt::ui::noteVisual`.

## 10. ImGui 1.92 specifics & gotchas

- Vendored ImGui is **1.92.9 (master)** with matching win32 + dx11 backends and
  `misc/cpp/imgui_stdlib` (for `InputText(label, std::string*)`).
- Fonts: the main UI font is the default (ProggyClean) → **ASCII glyphs only**, so do
  **not** use non-ASCII glyphs (✓, —, •, …) in UI strings; they render as boxes. Use
  `FULL`, `-`, `|`, etc. **Exception:** cell *note* data can be Hebrew — it uses a
  separate Hebrew-capable font pushed only around the note widgets (§9c).
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

- **Core logic:** `mingw32-make test` (191 checks: scoring rules incl. sub-question
  sync, one-press `stepAwarded`, and `isFullMarks`, JSON string + on-disk round-trip,
  recent-alias regression, `config <-> JSON` round-trip incl. the autosave record (§8c),
  plus `Cell`/`Student` `operator==` and `firstGradingDiff` for the undo history — §8b).
  Add cases when you touch model, scoring, or serialization.
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
- ASCII-only in UI **chrome** strings (font limitation, see §10). User *note* data
  is exempt — it renders through the Hebrew-capable notes font (§9c).
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
