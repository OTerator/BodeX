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

    void renderMenuBar();
    void applyLoadedProject(gt::Project&& p, const std::string& path);

    bool quit_ = false;
};
