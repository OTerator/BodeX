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

// App lives in the global namespace to match main.cpp's `App app;`.
class App {
public:
    App();
    ~App();

    void render();                       // draw the whole UI for this frame
    bool wantsQuit() const { return quit_; }

    enum class Screen { Home, NewProject, Grading };

    // ---- state read/written by ui/* screen functions ----
    Screen              screen = Screen::Home;
    bool                hasProject = false;
    gt::Project         project;
    std::string         projectPath;     // UTF-8 path of the .json ("" = unsaved)
    bool                dirty = false;
    gt::AppConfig       config;
    gt::NewProjectDraft draft;
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
    bool        gridEditPageActive = false; // stepped into the last-page field (via Space)
    std::string gridEditPageBuf;            // inline last-page text buffer
    bool        gridEditPageFocus = false;  // SetKeyboardFocusHere on the page field next draw
    bool        gridScrollToActive = false; // scroll active cell into view next draw
    bool        anyPreviewFocused = false;  // an image-preview window has the keyboard
                                            // (recomputed each frame; grid keys yield to it)

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
    int               addImageRole = 0;     // 0 = Question, 1 = Solution
    std::string       addImageCaption;
    std::vector<char> addImageSubs;         // per sub-question checkbox state

    // ---- actions invoked by screens/menu ----
    void newProjectStart();                          // -> NewProject screen, fresh draft
    void createProjectFromDraft();                   // build project -> Grading screen
    void openProjectDialog();                        // native open dialog -> load
    bool openProjectPath(const std::string& path);   // load a specific .json
    bool doSave();                                   // save to projectPath, or Save As if none
    bool doSaveAs();                                 // native save dialog -> save
    void closeProject();                             // guarded return to Home
    void requestQuit();                              // guarded quit
    void markDirty() { dirty = true; }

private:
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
    void applyLoadedProject(gt::Project&& p, const std::string& path);

    bool quit_ = false;
};
