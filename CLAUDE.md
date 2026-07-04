# BodeX — notes for Claude sessions

**Read [`spec.md`](spec.md) first.** It is the full contributor guide (intent,
architecture, data model, invariants, gotchas). This file is the always-loaded
short version: house rules, a doc-sync checklist, and a quick reference.

BodeX is a private Windows desktop app for tracking exam grading (students ×
questions grid). C++20 + Dear ImGui (Win32 + Direct3D 11), built with MinGW into
one self-contained `build/BodeX.exe`. No CMake, no external installs — ImGui and
nlohmann/json are vendored under `third_party/`.

---

## Keep the docs current (part of every change, not an afterthought)

`spec.md` is the source of truth for how this project is built and why. **When a
change alters behavior, structure, invariants, build, or workflow, update
`spec.md` in the same change** — a stale spec is worse than none. Likewise keep
`README.md` (user-facing) and `NOTES.md` (parked work) honest.

**"If you change ___, also update ___":**

| You changed… | Also update… |
|---|---|
| Data model (`Cell`/`Question`/`Student`/`Project` in `model/Project.h`) | `Serialization.cpp` (`*ToJson`/`*FromJson`), the cell/question editors (`CellEditor.cpp`, `NewProjectScreen.cpp`), the grid display (`cellSummary` in `GradingTable.cpp`), `tests/test_core.cpp`, spec.md §5/§11. **Bump `schemaVersion` if the change is breaking** and handle old files in `projectFromJsonString`. |
| Scoring rules (`Scoring.cpp`) | `tests/test_core.cpp`, spec.md §6. Respect precedence: `noSubmission` > `fullTick` > `clamp(awarded, effectiveMax)`; skipped sub-questions lock out points (`lockedSubPoints`). |
| A UI interaction or screen | `README.md` "Using it", `docs/screenshot.png` if it changed visibly, spec.md §8/§9. |
| Build includes/flags (`Makefile`) | `compile_flags.txt` (keep the two in sync so clangd resolves headers), spec.md §3. |
| Added/removed a third-party lib | `Makefile` `INCLUDES` + source lists, `compile_flags.txt`, README "License & credits", commit the vendored files, spec.md §4/§15. |
| Demo modes / env vars / shortcut | spec.md §3, README, `tools/create_shortcut.ps1`. |
| Finished or added a parked idea | `NOTES.md` and spec.md §14. |

---

## Quickstart

```sh
mingw32-make          # -> build/BodeX.exe
mingw32-make run
mingw32-make test     # non-GUI core tests (scoring, JSON/file round-trip, regressions)
mingw32-make clean
```

Try the UI without setup: run with `BODEX_DEMO=1` (grid), `=2` (grid + cell
editor open), or `=3` (New Project screen). Demo data is in-memory only.

---

## Invariants — do not regress these

- **Model layer stays GUI-free** (`src/model/*`, `src/util/utf.h`). Only file/path
  code there touches Win32. Keeps it unit-testable.
- **Scoring precedence:** `noSubmission` (row → 0) > `fullTick` (→ full points) >
  `clamp(awarded, 0, effectiveMax)`. **Skipped sub-questions lock out their points**
  (`effectiveMax = maxPoints − lockedSubPoints`): Equal deducts `maxPoints/subCount`
  per skip, Custom the specific `subPoints`. (Pre-v2, `X/Y` was reference-only.)
- **Toggling/painting full marks must not set `cell.touched`** — a full cell
  counts via `fullTick`; leaving `touched` alone lets un-fulling a blank cell
  return to blank instead of a stray `0`.
- **ASCII-only UI strings** — the default ImGui font has no other glyphs (no ✓, —,
  •). Use `FULL`, `-`, `|`.
- Grid must stay rectangular — go through `ensureShape` after loading/structural
  changes.
- All model text/paths are UTF-8; convert at the Win32 boundary via `util/utf.h`.

---

## Definition of done (verify, don't assume)

1. `mingw32-make` builds with no warnings.
2. `mingw32-make test` passes; add/adjust cases when you touch model, scoring, or
   serialization.
3. For UI changes, **verify visually by screenshot** — this is a real Win32 window.
   Use `BODEX_DEMO` + `PrintWindow`, and make the *capturing* process DPI-aware
   (`SetProcessDpiAwarenessContext((IntPtr)-4)`) or the 150%-scaled window comes
   back blurry/virtualized. Full recipe (and click/drag automation) in spec.md §12.
4. Relevant docs updated per the table above.

---

## Environment (this machine)

Windows 11, high-DPI (~150%). MSYS2 MinGW-w64 `g++` + `mingw32-make` + `windres`
on PATH; MSYS2 at `C:\msys64`. **No CMake.** The `Makefile` uses MSYS2 `sh` for
recipes. clangd reads `compile_flags.txt`.

---

## Parked work (see `NOTES.md`)

- **Grades export** — CSV first, then match an Excel format the user will provide.
  Planned as a `File → Export…` entry. **Do not implement until the format is
  given.**
- Compact-cell eliding for very long `lastPage` values.

---

## Working agreements

- Prefer editing existing modules over adding new ones; screens are small
  immediate-mode `gt::ui` functions taking `App&`.
- Match surrounding style and comment density.
- **Only commit or push when the user explicitly asks.** Repo: `OTerator/BodeX`
  (public, MIT). `build/` and `.claude/settings.local.json` are git-ignored;
  vendored `third_party/` is committed so clones build with no downloads.
