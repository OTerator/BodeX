#include "app/App.h"

#include "ui/HomeScreen.h"
#include "ui/NewProjectScreen.h"
#include "ui/GradingTable.h"
#include "ui/platform_dialogs.h"

#include "model/Scoring.h"
#include "model/Serialization.h"
#include "model/AppConfig.h"
#include "model/Assets.h"
#include "ui/ImageStore.h"

#include "imgui.h"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

// ------------------------------------------------------------------ Draft ---
namespace gt {

void NewProjectDraft::syncQuestions()
{
    if (questionCount < 1)
        questionCount = 1;
    const int old = static_cast<int>(questions.size());
    if (old != questionCount)
        questions.resize(static_cast<size_t>(questionCount));
    for (int j = old; j < questionCount; ++j) {
        Question q;
        q.title = "Q" + std::to_string(j + 1);
        q.maxPoints = 10.0;
        q.subCount = 1;
        q.split = SplitMode::Equal;
        normalizeQuestion(q);
        questions[static_cast<size_t>(j)] = q;
    }
    for (auto& q : questions)
        if (static_cast<int>(q.subPoints.size()) != q.subCount)
            normalizeQuestion(q);
}

void NewProjectDraft::reset()
{
    name = "New Exercise";
    studentCount = 20;
    questionCount = 4;
    questions.clear();
    syncQuestions();
}

} // namespace gt

// -------------------------------------------------------------------- App ---
// Populate an in-memory sample project. Used only when BODEX_DEMO is
// set, to eyeball the grading screen without clicking through setup.
static gt::Project buildDemoProject()
{
    gt::Question q1; q1.title = "Q1"; q1.maxPoints = 20; q1.subCount = 5; q1.split = gt::SplitMode::Equal;
    gt::Question q2; q2.title = "Q2"; q2.maxPoints = 10; q2.subCount = 2; q2.split = gt::SplitMode::Custom; q2.subPoints = {7, 3};
    gt::Question q3; q3.title = "Q3"; q3.maxPoints = 15; q3.subCount = 3; q3.split = gt::SplitMode::Equal;

    gt::Project p = gt::makeProject("Demo Exercise", 8, {q1, q2, q3});
    p.createdIso = gt::nowIso();

    // Equal-split deduction: Q1 has 5 sub-qs (4 pts each); 2 skipped -> 8 locked out.
    p.students[0].cells[0].awarded = 12; p.students[0].cells[0].subAnswered = 3;
    p.students[0].cells[0].touched = true; p.students[0].cells[0].lastPage = "14";
    p.students[0].cells[0].note = "recheck part b";
    p.students[0].cells[1].fullTick = true; p.students[0].cells[1].touched = true;
    p.students[1].noSubmission = true;
    p.students[2].cells[0].awarded = 25; p.students[2].cells[0].touched = true; // over max
    // Custom-split deduction: Q2 sub-points {7,3}; student 4 skipped the 3-pt part.
    p.students[3].cells[1].subChecks = {1, 0}; p.students[3].cells[1].subAnswered = 1;
    p.students[3].cells[1].awarded = 7; p.students[3].cells[1].touched = true;
    return p;
}

App::App()
{
    config = gt::loadConfig();
    draft.reset();

    // Open a project passed via env (used for "open with" / testing).
    if (const char* op = std::getenv("BODEX_OPEN"); op && *op) {
        openProjectPath(op);
        return;
    }

    if (const char* d = std::getenv("BODEX_DEMO"); d && *d && *d != '0') {
        if (*d == '3') {                // BODEX_DEMO=3 jumps to the New Project screen
            newProjectStart();
        } else {
            project = buildDemoProject();
            hasProject = true;
            dirty = true;
            screen = Screen::Grading;
            assetsDir = gt::liveAssetsDir(project, projectPath);
            if (*d == '2') {            // BODEX_DEMO=2 also opens the cell editor
                editorStudent = 0;
                editorQuestion = 0;
                requestOpenCellEditor = true;
            }
            statusMsg = "Demo project (BODEX_DEMO) - not saved to disk.";
        }
    }
}

App::~App() = default;

void App::render()
{
    // Global keyboard shortcuts.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) && hasProject)
        doSave();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        guard(Pending::NewProject);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O))
        guard(Pending::OpenDialog);

    // The bare grading grid drives its own keyboard (arrows/Tab/type-to-edit), so
    // turn ImGui's built-in keyboard nav OFF there or it would fight us (steal Tab,
    // draw a focus rect, move focus). Keep it ON for popups and other screens.
    {
        ImGuiIO& io = ImGui::GetIO();
        const bool anyPopup = ImGui::IsPopupOpen(nullptr,
            ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (screen == Screen::Grading && !anyPopup)
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        else
            io.ConfigFlags |=  ImGuiConfigFlags_NavEnableKeyboard;
    }

    renderMenuBar();

    switch (screen) {
        case Screen::Home:       gt::ui::homeScreen(*this);       break;
        case Screen::NewProject: gt::ui::newProjectScreen(*this); break;
        case Screen::Grading:    gt::ui::gradingScreen(*this);    break;
    }

    renderUnsavedPrompt();
}

void App::renderMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Project...", "Ctrl+N"))
            guard(Pending::NewProject);
        if (ImGui::MenuItem("Open Project...", "Ctrl+O"))
            guard(Pending::OpenDialog);
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, hasProject))
            doSave();
        if (ImGui::MenuItem("Save As...", nullptr, false, hasProject))
            doSaveAs();
        ImGui::Separator();
        if (ImGui::MenuItem("Close Project", nullptr, false, hasProject))
            guard(Pending::CloseProject);
        if (ImGui::MenuItem("Exit", "Alt+F4"))
            guard(Pending::Quit);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("BodeX - grading tracker", nullptr, false, false);
        ImGui::MenuItem("Green cell = full marks; ID -> No submission = 0", nullptr, false, false);
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

// ------------------------------------------------------- unsaved-work guard --
void App::guard(Pending next, const std::string& openPath)
{
    pending_ = next;
    pendingOpenPath_ = openPath;
    if (dirty && hasProject)
        openGuardPopup_ = true;   // deferred; opened in renderUnsavedPrompt (root id stack)
    else
        performPending();
}

void App::performPending()
{
    const Pending act = pending_;
    pending_ = Pending::None;
    switch (act) {
        case Pending::NewProject:   newProjectStart();                 break;
        case Pending::OpenDialog:   openProjectDialog();               break;
        case Pending::OpenPath:     openProjectPath(pendingOpenPath_); break;
        case Pending::CloseProject: closeProject();                    break;
        case Pending::Quit:         quit_ = true;                      break;
        case Pending::None:                                            break;
    }
}

void App::renderUnsavedPrompt()
{
    // Open here (not in guard) so OpenPopup and BeginPopupModal share this call's
    // id stack; a guard() from a File-menu item runs under the menu's id stack and
    // its OpenPopup would never match this modal.
    if (openGuardPopup_) {
        ImGui::OpenPopup("Unsaved Changes");
        openGuardPopup_ = false;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted("This project has unsaved changes.");
    ImGui::Spacing();

    if (ImGui::Button("Save", ImVec2(110, 0))) {
        if (doSave()) {            // may open Save As; only proceed if it succeeds
            ImGui::CloseCurrentPopup();
            performPending();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(110, 0))) {
        dirty = false;
        ImGui::CloseCurrentPopup();
        performPending();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) {
        pending_ = Pending::None;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// ----------------------------------------------------------------- actions --
void App::newProjectStart()
{
    draft.reset();
    statusMsg.clear();
    screen = Screen::NewProject;
}

void App::createProjectFromDraft()
{
    draft.syncQuestions();
    project = gt::makeProject(draft.name, draft.studentCount, draft.questions);
    project.createdIso = gt::nowIso();
    hasProject = true;
    projectPath.clear();
    dirty = true;                 // new, unsaved
    screen = Screen::Grading;
    assetsDir = gt::liveAssetsDir(project, projectPath); // staging until first save
    statusMsg = "New project created - use Save to write it to disk.";
}

void App::openProjectDialog()
{
    std::string path;
    if (gt::ui::openProjectDialog(path, gt::projectsDir()))
        openProjectPath(path);
}

bool App::openProjectPath(const std::string& path)
{
    gt::Project p;
    std::string err;
    if (!gt::loadProject(path, p, &err)) {
        statusMsg = "Open failed: " + err;
        return false;
    }
    applyLoadedProject(std::move(p), path);
    return true;
}

void App::applyLoadedProject(gt::Project&& p, const std::string& path)
{
    gt::ui::imageStoreReleaseAll(); // drop any previous project's textures
    previews.clear();               // stale windows would index into the old project
    activeRow = activeCol = 0;      // selection indexes into the new grid
    gridEditing = false;            // drop any half-finished inline edit
    project = std::move(p);
    hasProject = true;
    projectPath = path;
    dirty = false;
    screen = Screen::Grading;
    assetsDir = gt::liveAssetsDir(project, path);
    gt::addRecentProject(config, path);
    gt::saveConfig(config);
    statusMsg = "Opened " + path;
}

bool App::doSave()
{
    if (!hasProject)
        return false;
    if (projectPath.empty())
        return doSaveAs();

    std::string err;
    if (!gt::saveProject(projectPath, project, &err)) {
        statusMsg = "Save failed: " + err;
        return false;
    }

    // Migrate image files into the project's .assets folder (from staging on the
    // first save, or from the old location on Save As). No-op on a plain re-save.
    const std::string newDir = gt::projectAssetsDir(projectPath);
    if (!newDir.empty() && newDir != assetsDir) {
        std::vector<std::string> files;
        for (const auto& q : project.questions)
            for (const auto& im : q.images)
                if (!im.file.empty())
                    files.push_back(im.file);
        gt::syncImages(assetsDir, newDir, files);
        gt::ui::imageStoreReleaseAll(); // paths changed; reload from the new dir
        assetsDir = newDir;
    }

    dirty = false;
    gt::addRecentProject(config, projectPath);
    gt::saveConfig(config);
    statusMsg = "Saved " + projectPath;
    return true;
}

bool App::doSaveAs()
{
    if (!hasProject)
        return false;
    std::string path;
    const std::string suggested = (project.name.empty() ? "project" : project.name) + ".json";
    if (!gt::ui::saveProjectDialog(path, gt::projectsDir(), suggested))
        return false;
    projectPath = path;
    return doSave();
}

void App::closeProject()
{
    gt::ui::imageStoreReleaseAll();
    hasProject = false;
    project = gt::Project{};
    projectPath.clear();
    assetsDir.clear();
    dirty = false;
    editorStudent = editorQuestion = menuStudent = -1;
    imageMenuQuestion = -1;
    previews.clear();
    addImagePendingFile.clear();
    activeRow = activeCol = 0;
    gridEditing = false;
    screen = Screen::Home;
    statusMsg = "Project closed.";
}

void App::requestQuit()
{
    guard(Pending::Quit);
}
