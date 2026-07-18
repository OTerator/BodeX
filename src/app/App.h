#pragma once

// App is the top-level application object and simple screen state machine. The
// Win32/DX11 host (main.cpp) calls render() once per ImGui frame and checks
// wantsQuit(). Screen drawing lives in ui/* free functions that take App& and
// read/mutate the public state below (immediate-mode style).

#include <string>
#include <vector>

#include "model/Project.h"
#include "model/AppConfig.h"

namespace gt {

// Transient state for the "New Project" configuration screen.
struct NewProjectDraft {
    std::string           name = "New Exercise";
    int                   studentCount = 20;
    int                   questionCount = 4;
    std::vector<Question> questions; // sized to questionCount

    // Resize `questions` to questionCount, giving new questions sane defaults
    // and a title like "Q3".
    void syncQuestions();
    // Reset to defaults (called when starting a brand-new project).
    void reset();
};

// Transient working copy for the "Project Settings" screen (post-creation editing).
// Edits a copy of the live structure so the user can Cancel with no side effects;
// `originalIndex` is parallel to `questions` (-1 = a column added on this screen) and
// tells reshapeProject which columns carry their existing grades. Applied via
// App::applyProjectSettings -> gt::reshapeProject.
struct ProjectSettingsDraft {
    std::string           name;
    int                   studentCount = 0;
    int                   origStudentCount = 0;
    std::vector<Question> questions;
    std::vector<int>      originalIndex; // parallel to questions; -1 = new column

    // Snapshot the current structure of `p` into this draft.
    void loadFrom(const Project& p);
};

} // namespace gt

// One open, non-modal image-preview window. Several can be open at once (e.g. a
// question screenshot beside its solution). `question`/`image` index into
// project.questions[q].images[k]; `id` gives the ImGui window a stable "###id"
// and a focus target. Kept ImGui-free (plain scalars) so App.h stays light.
struct PreviewWin {
    int   id        = 0;
    int   question  = -1;   // index into project.questions
    int   image     = -1;   // index into question.images
    float zoom      = 1.0f;
    bool  fit       = true; // fit-to-window (recomputed each frame) vs manual zoom
    bool  focusNext = false; // raise/focus this window next frame (open-or-raise)
    bool  open      = true;  // cleared -> window closed, erased after the draw loop
};

// One open, non-modal notes-viewer window (badge click on a noted cell). Lists all
// of one cell's notes line-by-line; several can be open at once, one per cell.
struct NotesWin {
    int  id        = 0;
    int  student    = -1;   // index into project.students
    int  question   = -1;   // index into project.questions
    bool focusNext  = false; // raise/focus this window next frame (open-or-raise)
    bool open       = true;  // cleared -> window closed, erased after the draw loop
};

// App lives in the global namespace to match main.cpp's `App app;`.
class App {
public:
    App();
    ~App();

    void render();                       // draw the whole UI for this frame
    bool wantsQuit() const { return quit_; }

    enum class Screen { Home, NewProject, Grading, ProjectSettings };

    // ---- state read/written by ui/* screen functions ----
    Screen              screen = Screen::Home;
    bool                hasProject = false;
    gt::Project         project;
    std::string         projectPath;     // UTF-8 path of the .json ("" = unsaved)
    bool                dirty = false;
    gt::AppConfig       config;
    gt::NewProjectDraft draft;
    gt::ProjectSettingsDraft settings;   // working copy for the Project Settings screen
    std::string         statusMsg;       // transient message shown in the status bar

    // Cell editor popup target (-1 = none). requestOpenCellEditor is set the
    // frame a cell is clicked; the popup is opened after the table is drawn.
    int  editorStudent = -1;
    int  editorQuestion = -1;
    bool requestOpenCellEditor = false;

    // Student (row header) context popup target.
    int  menuStudent = -1;
    bool requestOpenStudentMenu = false;

    // Left-click / drag "paint full marks" gesture state (grading grid).
    bool paintActive = false;
    bool paintIsDrag = false;   // became a drag (moved onto another cell)
    int  paintAnchorRow = -1;   // student index the gesture started on
    int  paintAnchorCol = -1;   // question index the gesture started on
    int  paintAxis = 0;         // 0 = undecided, 1 = row, 2 = column

    // Keyboard-first grading: selected ("active") cell + inline numeric quick-entry.
    int         activeRow = 0;
    int         activeCol = 0;
    bool        gridEditing = false;        // inline awarded-points edit in progress
    std::string gridEditBuf;                // inline edit text buffer (score)
    bool        gridEditFocus = false;      // SetKeyboardFocusHere on next draw
    bool        gridEditDeselect = false;   // collapse InputText's auto-select-all once focused
    bool        gridEditScoreDirty = false; // the score field was actually edited (commit gate)
    bool        gridEditSuppressSpace = false; // ignore Space for the frame a Space-open fired
    bool        gridEditPageActive = false; // stepped into the last-page field (via Space)
    std::string gridEditPageBuf;            // inline last-page text buffer
    bool        gridEditPageFocus = false;  // SetKeyboardFocusHere on the page field next draw
    bool        gridScrollToActive = false; // scroll active cell into view next draw
    bool        anyPreviewFocused = false;  // an image-preview window has the keyboard
                                            // (recomputed each frame; grid keys yield to it)
    bool        showShortcuts = false;      // F1 toggles the grid shortcuts help overlay

    // Column view controls (zoom / fold / multi-select). gridZoom is session-only;
    // per-question `folded`/`viewWidth` live on the model (persisted). See GradingTable.
    float             gridZoom = 1.0f;       // per-view grid scale (Ctrl+scroll)
    bool              gridReflow = false;    // force column widths this frame (zoom/fold/fit)
    bool              focusMode = false;     // session-only one-question focus view (lifecycle mirrors gridZoom)
    std::vector<char> headerSel;             // per-question header multi-select mask
    int               headerSelAnchor = -1;  // Shift+click range anchor
    int               colMenuQuestion = -1;  // right-clicked header -> ColumnMenu target
    bool              requestOpenColMenu = false;

    // Question images: where this project's image files currently live.
    std::string assetsDir;

    // Column-header image menu target.
    int   imageMenuQuestion = -1;
    bool  requestOpenImageMenu = false;

    // Open image-preview windows (non-modal; can stay open beside the grid).
    std::vector<PreviewWin> previews;
    int                     nextPreviewId = 1; // hands out stable per-window ids

    // Transient "Add image" form (inside the image menu popup).
    std::string       addImagePendingFile; // picked source path awaiting confirm
    bool              addImagePasteTemp = false; // pending file is a clipboard temp (delete after)
    int               addImageRole = 0;     // 0 = Question, 1 = Solution
    std::string       addImageCaption;
    std::vector<char> addImageSubs;         // per sub-question checkbox state

    // Per-sub-question notes: the cell editor's current note target (-1 = general,
    // else a 0-based sub-question index) and a "pending suggestion" commit latch —
    // the (student, question, sub) last edited, so a picked note gets offered back as
    // a suggestion once the edit settles (target switch, editor close, or save).
    int  editorNoteSub      = -1;
    int  noteCommitStudent  = -1;
    int  noteCommitQuestion = -1;
    int  noteCommitSub      = -1;
    bool noteTargetValid    = false; // the commit latch above targets a real edit
    bool editorWasOpen      = false; // last-frame CellEditor open state (close detection)

    // Column-header sub-question-labels popup target.
    int  labelsQuestion        = -1;
    bool requestOpenSubLabels  = false;

    // Open notes-viewer windows (badge click on a noted cell; non-modal, several at once).
    std::vector<NotesWin> notesWins;
    int                    nextNotesWinId = 1;

    // Offer the current note-edit target's live text as a suggestion (append-only,
    // exact-dedup) if it's non-empty. Idempotent, so it's safe to call at every edit
    // boundary (target switch, editor close, save).
    void commitPendingNoteSuggestion();

    // ---- actions invoked by screens/menu ----
    void newProjectStart();                          // -> NewProject screen, fresh draft
    void createProjectFromDraft();                   // build project -> Grading screen
    void openProjectDialog();                        // native open dialog -> load
    bool openProjectPath(const std::string& path);   // load a specific .json
    bool doSave();                                   // save to projectPath, or Save As if none
    bool doSaveAs();                                 // native save dialog -> save
    void closeProject();                             // guarded return to Home
    void requestQuit();                              // guarded quit

    // ---- project settings (post-creation structure editing; see App.cpp §8d) ----
    void openProjectSettings();     // snapshot structure into `settings` -> settings screen
    void tryApplyProjectSettings(); // apply now if grades are safe, else raise the confirm modal
    std::vector<std::string> settingsChangeSummary(); // grade-affecting effects of the pending edit
    void flushAutosave();                            // write the pending autosave now (focus-loss / close)
    void markDirty() { dirty = true; undoPending_ = true; } // also arms an undo checkpoint

    // ---- undo / redo (grading edits only; see App.cpp) ----
    void undo();                                     // revert the last grading action
    void redo();                                     // reapply an undone action
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

private:
    // Snapshot-coalescing history over the grading grid (project.students only —
    // images live under project.questions and are excluded). A snapshot is a deep
    // copy of the students vector; ~tens of KB, so per-action copies are cheap.
    std::vector<std::vector<gt::Student>> undoStack_;    // past grading states
    std::vector<std::vector<gt::Student>> redoStack_;    // undone states, for redo
    std::vector<gt::Student>              undoBaseline_; // last-committed grading state
    bool                                  undoPending_ = false; // markDirty since baseline
    static constexpr size_t kUndoDepth = 100;           // cap; oldest dropped past this

    void maybeCommitUndo();     // end-of-frame: checkpoint once an action has settled
    void resetHistory();        // clear stacks + reseed baseline (new/open/close)
    void abortInProgressEdit(); // cancel any inline edit / paint gesture before a restore
    void clampActive();         // keep activeRow/activeCol inside the grid
    void resetColumnView();     // reset zoom/selection; force widths from the new grid

    // ---- autosave / crash recovery (see App.cpp §autosave) ----
    // Periodic crash-insurance writes to %APPDATA%\BodeX\autosave\<project.id>.autosave.
    // The config keeps a record pointing at the live file; it is deleted on every
    // clean exit, so a record that survives to the next launch means the last session
    // crashed and we offer to recover it.
    std::string autosaveTarget();   // the autosave path for the current project
    void maybeAutosave();           // end-of-frame: rate-limited write once the action has settled
    void writeAutosave();           // force-write the file + refresh the config record
    void clearAutosave();           // delete the recorded file + clear the record (clean exit)
    void restoreFromAutosave();     // adopt config.autosave's file as the live project
    void renderRestorePrompt();     // launch-time "Recover Unsaved Work" modal
    double lastAutosave_    = 0.0;  // ImGui::GetTime() of the last write
    double autosaveInterval_= 30.0; // seconds between ticks (BODEX_AUTOSAVE_SEC overrides)
    bool   demoMode_        = false; // BODEX_DEMO project -> never autosaved
    bool   openRestorePopup_= false; // deferred OpenPopup latch (mirrors openGuardPopup_)

    // Unsaved-changes guard: some actions must offer Save/Discard/Cancel first.
    enum class Pending { None, NewProject, OpenDialog, OpenPath, CloseProject, Quit };
    Pending     pending_ = Pending::None;
    std::string pendingOpenPath_;

    void guard(Pending next, const std::string& openPath = "");
    void performPending();
    void renderUnsavedPrompt();
    // Set by guard() when the prompt is needed; the actual OpenPopup is deferred
    // to renderUnsavedPrompt so it shares BeginPopupModal's id-stack. Opening it
    // directly from a menu item (different id stack) would never match the modal.
    bool openGuardPopup_ = false;

    void renderMenuBar();
    void renderShortcutsOverlay(); // F1 / Help -> "Keyboard Shortcuts" legend window
    void applyLoadedProject(gt::Project&& p, const std::string& path);

    // Project Settings: commit `settings` to the live project and the confirm modal.
    void applyProjectSettings();     // reshape the project + reset view/history, -> Grading
    void renderSettingsConfirm();    // "Apply Changes?" modal (deferred-open, §8 id-stack)
    bool openSettingsConfirm_ = false;        // deferred OpenPopup latch (mirrors openGuardPopup_)
    std::vector<std::string> settingsSummary_; // effects shown in the confirm modal

    bool quit_ = false;
};
