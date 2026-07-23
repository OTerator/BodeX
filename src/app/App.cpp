#include "app/App.h"

#include "ui/HomeScreen.h"
#include "ui/NewProjectScreen.h"
#include "ui/SettingsScreen.h"
#include "ui/GradingTable.h"
#include "ui/platform_dialogs.h"

#include "model/Scoring.h"
#include "model/Serialization.h"
#include "model/AppConfig.h"
#include "model/Assets.h"
#include "ui/ImageStore.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

void ProjectSettingsDraft::loadFrom(const Project& p)
{
    name             = p.name;
    studentCount     = static_cast<int>(p.students.size());
    origStudentCount = studentCount;
    questions        = p.questions;
    originalIndex.resize(questions.size());
    for (size_t j = 0; j < questions.size(); ++j)
        originalIndex[j] = static_cast<int>(j); // each column starts mapped to itself
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

    // Sub-question labels (Hebrew) on Q1, so the cell editor's target chips and the
    // notes badge/window have something besides numeric fallbacks to show off.
    p.questions[0].subLabels = { "\xD7\x90", "\xD7\x91", "", "", "" }; // Aleph, Bet, then numeric fallback
    // Seed the suggestion pool (one general, one per-sub) so the cell editor's
    // suggestion list isn't empty on first look.
    p.questions[0].noteSuggestions = {
        gt::NoteSuggestion{-1, "nice work overall", gt::TextDir::Auto},
        gt::NoteSuggestion{0, "recheck this part", gt::TextDir::Auto},
    };

    // Equal-split deduction: Q1 has 5 sub-qs (4 pts each); 2 skipped -> 8 locked out.
    p.students[0].cells[0].awarded = 12; p.students[0].cells[0].subAnswered = 3;
    p.students[0].cells[0].touched = true; p.students[0].cells[0].lastPage = "14";
    // Hebrew note (UTF-8 literal; means "recheck section 2") shows off the BiDi/RTL
    // notes support — the trailing "2" stays left-to-right inside the RTL text.
    p.students[0].cells[0].note = "בדוק סעיף 2";
    p.students[0].cells[0].noteDir = gt::TextDir::Auto;
    // A per-sub-question note on sub 0 (labeled Aleph above), so the badge and the
    // notes window show more than just the general note.
    gt::setSubNote(p.students[0].cells[0], 0, "לבדוק שוב", gt::TextDir::Auto);
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

    // Autosave cadence (seconds): the saved preference, then BODEX_AUTOSAVE_SEC as an
    // override so the crash-recovery flow can be exercised quickly during testing.
    if (config.prefs.autosaveSec > 0.0)
        autosaveInterval_ = config.prefs.autosaveSec;
    if (const char* iv = std::getenv("BODEX_AUTOSAVE_SEC"); iv && *iv) {
        const double v = std::atof(iv);
        if (v > 0.0)
            autosaveInterval_ = v;
    }

    // Open a project passed via env (used for "open with" / testing).
    if (const char* op = std::getenv("BODEX_OPEN"); op && *op) {
        openProjectPath(op);
        return;
    }

    if (const char* d = std::getenv("BODEX_DEMO"); d && *d && *d != '0') {
        if (*d == '3') {                // BODEX_DEMO=3 jumps to the New Project screen
            newProjectStart();
        } else {
            demoMode_ = true;           // in-memory demo: never autosaved / recovered
            project = buildDemoProject();
            hasProject = true;
            dirty = true;
            screen = Screen::Grading;
            assetsDir = gt::liveAssetsDir(project, projectPath);
            resetHistory();
            if (*d == '2') {            // BODEX_DEMO=2 also opens the cell editor
                editorStudent = 0;
                editorQuestion = 0;
                requestOpenCellEditor = true;
            }
            if (*d == '4') {            // BODEX_DEMO=4 opens the Settings panel
                settingsSection = SettingsSection::General;
                screen = Screen::Settings;
            }
            statusMsg = "Demo project (BODEX_DEMO) - not saved to disk.";
        }
    }

    // A pending autosave that outlived its session (we're on Home with no project,
    // its file still exists) means the last run crashed -> offer to recover it.
    if (!hasProject && screen == Screen::Home &&
        !config.autosave.empty() && gt::fileExists(config.autosave.file))
        openRestorePopup_ = true;
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
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false))    // F1: toggle the shortcuts overlay
        showShortcuts = !showShortcuts;

    // Undo/redo (grading grid only). Gated so Ctrl+Z falls through to ImGui's
    // built-in text undo while an input is active (inline edit or a popup field),
    // and so a modal popup isn't undone out from under. IsKeyChordPressed matches
    // modifiers exactly, so Ctrl+Z (undo) and Ctrl+Shift+Z (redo) don't collide.
    {
        const bool anyPopup = ImGui::IsPopupOpen(nullptr,
            ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (screen == Screen::Grading && !gridEditing && !anyPopup &&
            !ImGui::IsAnyItemActive()) {
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
                undo();
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
                ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
                redo();
        }
        // F3 toggles the one-question Focus view (same gate: not mid-edit / mid-popup).
        if (screen == Screen::Grading && !gridEditing && !anyPopup &&
            ImGui::IsKeyPressed(ImGuiKey_F3, false))
            focusMode = !focusMode;
    }

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
        case Screen::Settings:   gt::ui::settingsScreen(*this);   break;
    }

    renderShortcutsOverlay();  // F1 / Help legend; drawn over any screen
    renderUnsavedPrompt();
    renderSettingsConfirm();
    renderRestorePrompt();

    // After everything is drawn: checkpoint the frame's grading edit (if any) once
    // the action has settled, then take a rate-limited autosave under the same
    // settle gate. Placed last so paint/inline-edit flags reflect the end-of-frame
    // state.
    maybeCommitUndo();
    maybeAutosave();
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
        if (ImGui::MenuItem("Project Settings...", nullptr, false, hasProject))
            openSettings(SettingsSection::Project);
        ImGui::Separator();
        if (ImGui::MenuItem("Close Project", nullptr, false, hasProject))
            guard(Pending::CloseProject);
        if (ImGui::MenuItem("Exit", "Alt+F4"))
            guard(Pending::Quit);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo()))
            undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo()))
            redo();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Settings")) {
        if (ImGui::MenuItem("Settings...", nullptr, screen == Screen::Settings))
            openSettings(SettingsSection::General);
        ImGui::Separator();
        if (ImGui::MenuItem("General"))
            openSettings(SettingsSection::General);
        if (ImGui::MenuItem("Keybinds"))
            openSettings(SettingsSection::Keybinds);
        if (ImGui::MenuItem("Project Settings...", nullptr, false, hasProject))
            openSettings(SettingsSection::Project);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("BodeX - grading tracker", nullptr, false, false);
        ImGui::MenuItem("Green cell = full marks; ID -> No submission = 0", nullptr, false, false);
        ImGui::Separator();
        // Live checkbox mirroring the overlay's open state; F1 toggles the same flag.
        ImGui::MenuItem("Keyboard Shortcuts", "F1", &showShortcuts);
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void App::renderShortcutsOverlay()
{
    // A plain-text window (no interactive widgets) so it never fights the grid's key
    // handling; NoFocusOnAppearing keeps the grid focused. Drawn at app level so the
    // Help menu / F1 reach it on every screen, not just while grading.
    if (!showShortcuts)
        return;
    ImGui::SetNextWindowBgAlpha(0.92f);
    if (ImGui::Begin("Keyboard Shortcuts (F1)", &showShortcuts,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::TextUnformatted(gt::ui::gridShortcutsText());
    }
    ImGui::End();
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
        case Pending::Quit:         clearAutosave(); quit_ = true;     break;
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
    clearAutosave();              // drop any outgoing project's autosave
    draft.syncQuestions();
    project = gt::makeProject(draft.name, draft.studentCount, draft.questions);
    project.createdIso = gt::nowIso();
    resetColumnView();            // fresh selection/zoom for the new grid
    hasProject = true;
    projectPath.clear();
    dirty = true;                 // new, unsaved
    screen = Screen::Grading;
    assetsDir = gt::liveAssetsDir(project, projectPath); // staging until first save
    resetHistory();
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
    clearAutosave();                // drop the outgoing project's autosave
    gt::ui::imageStoreReleaseAll(); // drop any previous project's textures
    previews.clear();               // stale windows would index into the old project
    notesWins.clear();              // ditto for the notes-viewer windows
    editorNoteSub = -1;
    noteTargetValid = false;
    editorWasOpen = false;
    labelsQuestion = -1;
    activeRow = activeCol = 0;      // selection indexes into the new grid
    gridEditing = false;            // drop any half-finished inline edit
    gridEditPageActive = false;
    gridEditScoreDirty = false;
    gridEditSuppressSpace = false;
    project = std::move(p);
    resetColumnView();              // zoom/selection reset; force loaded column widths
    hasProject = true;
    projectPath = path;
    dirty = false;
    screen = Screen::Grading;
    assetsDir = gt::liveAssetsDir(project, path);
    resetHistory();
    gt::addRecentProject(config, path);
    gt::saveConfig(config);
    statusMsg = "Opened " + path;
}

// ------------------------------------------------------ settings panel --
void App::openSettings(SettingsSection s)
{
    if (s == SettingsSection::Project) {
        if (!hasProject)
            return;                 // no project -> Project section is unavailable
        settings.loadFrom(project); // snapshot the live structure into the working copy
        statusMsg.clear();
    }
    settingsSection = s;
    screen = Screen::Settings;
}

// Apply the chosen theme, then scale the style once to the monitor DPI. Rebuilds
// from defaults each call (StyleColors* resets colours + sizes) so a theme switch
// never compounds the DPI scale. Called at launch and on a theme change.
void App::applyDisplaySettings()
{
    ImGui::StyleColorsDark(); // reset to defaults first (also restores default sizes)
    if (config.prefs.theme == 1)      ImGui::StyleColorsLight();
    else if (config.prefs.theme == 2) ImGui::StyleColorsClassic();

    ImGuiStyle& style = ImGui::GetStyle();
    if (baseDpi_ > 1.0f)
        style.ScaleAllSizes(baseDpi_);
    style.FontScaleMain = baseDpi_; // global font scale (grid cells use FontSizeBase*gridZoom)
}

// Push non-display prefs that live outside the ImGui style. The autosave interval
// mirrors config.prefs.autosaveSec, but the BODEX_AUTOSAVE_SEC env override (set in
// the ctor) still wins if present.
void App::applyPrefsRuntime()
{
    if (const char* iv = std::getenv("BODEX_AUTOSAVE_SEC"); iv && *iv && std::atof(iv) > 0.0)
        return; // env override is authoritative
    if (config.prefs.autosaveSec > 0.0)
        autosaveInterval_ = config.prefs.autosaveSec;
}

// ----------------------------------------------- project settings (§8d) --
void App::openProjectSettings()
{
    openSettings(SettingsSection::Project);   // Project is now a Settings-panel section
}

void App::tryApplyProjectSettings()
{
    settingsSummary_ = settingsChangeSummary();
    if (settingsSummary_.empty())
        applyProjectSettings();       // pure additions / renames -> nothing at risk
    else
        openSettingsConfirm_ = true;  // grade-affecting -> confirm (deferred open, §8)
}

void App::applyProjectSettings()
{
    // Reshape the live project to the edited structure, carrying grades per originalIndex.
    gt::reshapeProject(project, settings.questions, settings.originalIndex, settings.studentCount);
    project.name = settings.name;

    // A structural change gets the same view-state reset as a project swap: previews /
    // notes windows index into questions/students by position, and prior undo snapshots
    // are shaped for the old questions, so both are invalidated.
    previews.clear();
    notesWins.clear();
    editorNoteSub = -1;
    noteTargetValid = false;
    editorWasOpen = false;
    labelsQuestion = -1;
    activeRow = activeCol = 0;
    gridEditing = false;
    gridEditPageActive = false;
    gridEditScoreDirty = false;
    gridEditSuppressSpace = false;
    resetColumnView();
    resetHistory();     // old snapshots no longer match the grid shape -> discard
    markDirty();
    screen = Screen::Grading;
    statusMsg = "Project settings applied.";
}

std::vector<std::string> App::settingsChangeSummary()
{
    std::vector<std::string> out;
    const int nStudents = static_cast<int>(project.students.size());

    // 1. Deleted columns (an old index no originalIndex maps to) that carried grades.
    std::vector<char> kept(project.questions.size(), 0);
    for (int oi : settings.originalIndex)
        if (oi >= 0 && oi < static_cast<int>(kept.size()))
            kept[static_cast<size_t>(oi)] = 1;
    for (size_t j = 0; j < project.questions.size(); ++j) {
        if (kept[j]) continue;
        bool graded = false;
        for (const auto& s : project.students) {
            if (j >= s.cells.size()) continue;
            const gt::Cell& c = s.cells[j];
            if (c.touched || c.fullTick || gt::cellHasAnyNote(c)) { graded = true; break; }
        }
        char buf[192];
        std::snprintf(buf, sizeof(buf), "Question \"%s\" will be removed%s.",
                      project.questions[j].title.c_str(),
                      graded ? " (discards its grades)" : "");
        out.emplace_back(buf);
    }

    // 2. Removed students (end-truncation) that carried grades.
    if (settings.studentCount < nStudents) {
        bool anyGraded = false;
        for (int i = settings.studentCount; i < nStudents && !anyGraded; ++i) {
            const gt::Student& s = project.students[static_cast<size_t>(i)];
            if (s.noSubmission) { anyGraded = true; break; }
            for (const auto& c : s.cells)
                if (c.touched || c.fullTick || gt::cellHasAnyNote(c)) { anyGraded = true; break; }
        }
        const int removed = nStudents - settings.studentCount;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%d student%s will be removed%s.",
                      removed, removed == 1 ? "" : "s",
                      anyGraded ? " (some carry grades)" : "");
        out.emplace_back(buf);
    }

    // 3. Retained columns whose structure change alters an existing cell's score.
    //    Build the reshape into a temp copy and compare per-cell before/after.
    gt::Project tmp = project;
    gt::reshapeProject(tmp, settings.questions, settings.originalIndex, settings.studentCount);
    for (size_t j = 0; j < settings.originalIndex.size() && j < tmp.questions.size(); ++j) {
        const int oi = settings.originalIndex[j];
        if (oi < 0 || oi >= static_cast<int>(project.questions.size()))
            continue; // a brand-new column has no "before" to change
        bool changed = false;
        const int rows = std::min(static_cast<int>(project.students.size()),
                                  static_cast<int>(tmp.students.size()));
        for (int i = 0; i < rows && !changed; ++i) {
            const gt::Student& sb = project.students[static_cast<size_t>(i)];
            const gt::Student& sa = tmp.students[static_cast<size_t>(i)];
            const gt::Cell& before = sb.cells[static_cast<size_t>(oi)];
            const gt::Cell& after  = sa.cells[j];
            if (!(before == after))
                changed = true; // reshaping altered the stored cell (e.g. reset Custom marks)
            else if (std::fabs(gt::cellPoints(sb, project.questions[static_cast<size_t>(oi)], before) -
                               gt::cellPoints(sa, tmp.questions[j], after)) > 1e-9)
                changed = true; // same cell, different score (e.g. equalShare shifted)
        }
        if (changed) {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "Question \"%s\": some existing scores will be recalculated.",
                          tmp.questions[j].title.c_str());
            out.emplace_back(buf);
        }
    }
    return out;
}

void App::renderSettingsConfirm()
{
    // Deferred open so OpenPopup/BeginPopupModal share this call's id stack (the Apply
    // button lives under the settings window's id stack; opening there would never match).
    if (openSettingsConfirm_) {
        ImGui::OpenPopup("Apply Changes?");
        openSettingsConfirm_ = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Apply Changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted("These changes affect existing grades:");
    ImGui::Spacing();
    for (const auto& line : settingsSummary_)
        ImGui::BulletText("%s", line.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("Grading undo history will be cleared.");
    ImGui::Spacing();

    if (ImGui::Button("Apply", ImVec2(110, 0))) {
        ImGui::CloseCurrentPopup();
        applyProjectSettings();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

bool App::doSave()
{
    if (!hasProject)
        return false;
    if (projectPath.empty())
        return doSaveAs();

    commitPendingNoteSuggestion(); // capture a mid-edit note-in-progress before writing

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
    clearAutosave();              // the .json is now the durable copy; drop the autosave
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
    clearAutosave();
    gt::ui::imageStoreReleaseAll();
    hasProject = false;
    project = gt::Project{};
    projectPath.clear();
    assetsDir.clear();
    dirty = false;
    editorStudent = editorQuestion = menuStudent = -1;
    imageMenuQuestion = -1;
    previews.clear();
    notesWins.clear();
    editorNoteSub = -1;
    noteTargetValid = false;
    editorWasOpen = false;
    labelsQuestion = -1;
    addImagePendingFile.clear();
    activeRow = activeCol = 0;
    gridEditing = false;
    gridEditPageActive = false;
    gridEditScoreDirty = false;
    gridEditSuppressSpace = false;
    resetColumnView();
    resetHistory();
    screen = Screen::Home;
    statusMsg = "Project closed.";
}

void App::requestQuit()
{
    flushAutosave();       // capture the latest edits before the guard/shutdown window
    guard(Pending::Quit);
}

// ------------------------------------------------------------- note suggestions --
//
// The cell editor sets the (student, question, sub) commit latch every frame it
// draws a note target (CellEditor.cpp); this offers that target's *current* model
// text back into the question's suggestion pool once the edit settles (switching
// targets, closing the editor, or saving). addNoteSuggestion is exact-dedup, so
// calling this at every boundary is safe/idempotent — it only ever adds once per
// distinct (sub, text).
void App::commitPendingNoteSuggestion()
{
    if (!noteTargetValid)
        return;
    noteTargetValid = false;

    if (noteCommitStudent < 0 || noteCommitStudent >= static_cast<int>(project.students.size()) ||
        noteCommitQuestion < 0 || noteCommitQuestion >= static_cast<int>(project.questions.size()))
        return;
    gt::Student& s = project.students[static_cast<size_t>(noteCommitStudent)];
    if (noteCommitQuestion >= static_cast<int>(s.cells.size()))
        return;
    gt::Cell&     c = s.cells[static_cast<size_t>(noteCommitQuestion)];
    gt::Question& q = project.questions[static_cast<size_t>(noteCommitQuestion)];

    std::string  text;
    gt::TextDir  dir;
    if (noteCommitSub < 0) {
        text = c.note;
        dir  = c.noteDir;
    } else {
        const gt::SubNote* sn = gt::findSubNote(c, noteCommitSub);
        if (!sn) return;
        text = sn->text;
        dir  = sn->dir;
    }
    if (gt::addNoteSuggestion(q, noteCommitSub, text, dir))
        markDirty();
}

// ----------------------------------------------------------- undo / redo -----
//
// The history is a coalescing snapshot stack over the grading grid
// (project.students). markDirty() arms undoPending_; maybeCommitUndo() runs at the
// end of every frame and, once the current action has *settled*, moves the previous
// baseline onto the undo stack and adopts the current grid as the new baseline. The
// settle gates (no paint drag, no inline edit, no active ImGui item) collapse a
// multi-frame action — a paint drag, a typed inline edit, or typing in a cell-editor
// field — into a single history entry. Image add/remove marks the project dirty too
// but leaves project.students unchanged, so the equality guard skips it: history is
// grading-data-only, matching the roadmap scope.

void App::resetHistory()
{
    undoStack_.clear();
    redoStack_.clear();
    undoBaseline_ = project.students; // current grid is the baseline for a fresh timeline
    undoPending_ = false;
}

void App::maybeCommitUndo()
{
    if (!undoPending_)
        return;
    if (screen != Screen::Grading) { undoPending_ = false; return; }
    // Don't checkpoint in the middle of a still-running action.
    if (paintActive || gridEditing || gridEditPageActive)
        return;
    if (ImGui::IsAnyItemActive())          // a cell-editor input is being edited
        return;
    undoPending_ = false;
    if (project.students == undoBaseline_)  // nothing grading-relevant changed
        return;
    undoStack_.push_back(std::move(undoBaseline_));
    if (undoStack_.size() > kUndoDepth)
        undoStack_.erase(undoStack_.begin());
    redoStack_.clear();                     // a fresh edit forks the timeline
    undoBaseline_ = project.students;       // deep copy of the now-current grid
}

void App::abortInProgressEdit()
{
    gridEditing = false;
    gridEditPageActive = false;
    gridEditScoreDirty = false;
    gridEditSuppressSpace = false;
    gridEditBuf.clear();
    gridEditPageBuf.clear();
    paintActive = false;
    paintIsDrag = false;
    paintAxis = 0;
}

void App::clampActive()
{
    const int rows = static_cast<int>(project.students.size());
    const int cols = static_cast<int>(project.questions.size());
    if (activeRow >= rows) activeRow = rows > 0 ? rows - 1 : 0;
    if (activeCol >= cols) activeCol = cols > 0 ? cols - 1 : 0;
    if (activeRow < 0) activeRow = 0;
    if (activeCol < 0) activeCol = 0;
}

// Reset the per-view column controls for a freshly adopted project: drop the zoom
// and header selection, and arm a reflow so the first grading frame applies each
// question's persisted viewWidth / folded state. headerSel is (re)sized in the grid
// draw, so clearing it here is enough. Call after `project` is assigned.
void App::resetColumnView()
{
    gridZoom = 1.0f;
    gridReflow = true;          // first frame forces widths from the loaded grid
    focusMode = false;          // start every project in the grid view
    headerSel.clear();
    headerSelAnchor = -1;
    colMenuQuestion = -1;
    requestOpenColMenu = false;
}

void App::undo()
{
    if (undoStack_.empty())
        return;
    abortInProgressEdit();
    redoStack_.push_back(project.students);          // current -> redo
    std::vector<gt::Student> prev = std::move(undoStack_.back());
    undoStack_.pop_back();
    const auto [r, c] = gt::firstGradingDiff(project.students, prev); // where it changed
    project.students = std::move(prev);
    undoBaseline_ = project.students;
    undoPending_ = false;
    dirty = true;                                    // file no longer matches disk
    if (r >= 0) { activeRow = r; activeCol = c; }     // jump the selection to the change
    clampActive();
    gridScrollToActive = true;
}

void App::redo()
{
    if (redoStack_.empty())
        return;
    abortInProgressEdit();
    undoStack_.push_back(project.students);          // current -> undo
    std::vector<gt::Student> next = std::move(redoStack_.back());
    redoStack_.pop_back();
    const auto [r, c] = gt::firstGradingDiff(project.students, next);
    project.students = std::move(next);
    undoBaseline_ = project.students;
    undoPending_ = false;
    dirty = true;
    if (r >= 0) { activeRow = r; activeCol = c; }
    clampActive();
    gridScrollToActive = true;
}

// --------------------------------------------------------- autosave / recovery
//
// Crash insurance: while a project has unsaved edits, a full copy is written to
// %APPDATA%\BodeX\autosave\<project.id>.autosave every autosaveInterval_ seconds
// (once the current action has settled), plus an immediate flush on focus-loss and
// before quit. The config keeps a record pointing at the live file; every clean exit
// deletes it (clearAutosave). A record that survives to the next launch means the app
// did not close cleanly, so App() arms renderRestorePrompt() to offer recovery. The
// autosave never replaces the user's .json -- an explicit Save is still the durable copy.

std::string App::autosaveTarget()
{
    if (project.id.empty())        // very old files may lack an id; the filename needs one
        project.id = gt::newProjectId();
    const std::string dir = gt::autosaveDir();
    if (dir.empty())
        return std::string();
    return dir + "\\" + project.id + ".autosave";
}

void App::writeAutosave()
{
    const std::string path = autosaveTarget();
    if (path.empty())
        return;
    std::string err;
    if (!gt::saveProject(path, project, &err))
        return;                    // best-effort; a failed autosave stays silent
    config.autosave = { path, projectPath, project.name, gt::nowIso() };
    gt::saveConfig(config);
    lastAutosave_ = ImGui::GetTime();
    statusMsg = "Autosaved " + config.autosave.savedIso;
}

void App::maybeAutosave()
{
    if (demoMode_ || !hasProject || screen != Screen::Grading || !dirty)
        return;
    // Don't write mid-action (the same settle gate as maybeCommitUndo).
    if (paintActive || gridEditing || gridEditPageActive || ImGui::IsAnyItemActive())
        return;
    if (ImGui::GetTime() - lastAutosave_ < autosaveInterval_)
        return;
    writeAutosave();
}

void App::flushAutosave()
{
    if (demoMode_ || !hasProject || !dirty)
        return;
    writeAutosave();               // ignore the interval: capture the tail now
}

void App::clearAutosave()
{
    if (config.autosave.empty())
        return;
    gt::removeFile(config.autosave.file); // delete the recorded file (survives a Save As move)
    config.autosave = gt::AutosaveRecord{};
    gt::saveConfig(config);
}

void App::restoreFromAutosave()
{
    const gt::AutosaveRecord rec = config.autosave; // copy before we mutate config
    gt::Project p;
    std::string err;
    if (!gt::loadProject(rec.file, p, &err)) {
        statusMsg = "Recovery failed: " + err;
        clearAutosave();           // unreadable autosave -> drop it
        return;
    }

    // Adopt like applyLoadedProject, but keep the recovered work marked dirty and
    // leave the autosave record in place (the next tick overwrites the same file).
    gt::ui::imageStoreReleaseAll();
    previews.clear();
    notesWins.clear();
    editorNoteSub = -1;
    noteTargetValid = false;
    editorWasOpen = false;
    labelsQuestion = -1;
    activeRow = activeCol = 0;
    gridEditing = false;
    gridEditPageActive = false;
    gridEditScoreDirty = false;
    gridEditSuppressSpace = false;
    project = std::move(p);
    resetColumnView();             // zoom/selection reset; force recovered column widths
    hasProject = true;
    projectPath = rec.projectPath; // original .json, or "" if it was never saved
    dirty = true;                  // recovered work diverges from disk
    screen = Screen::Grading;
    assetsDir = gt::liveAssetsDir(project, projectPath);
    resetHistory();
    lastAutosave_ = ImGui::GetTime(); // restart the autosave clock
    statusMsg = "Recovered unsaved work (autosaved " + rec.savedIso + ").";
}

void App::renderRestorePrompt()
{
    // Deferred open on the modal's own id stack (same reasoning as renderUnsavedPrompt).
    if (openRestorePopup_) {
        ImGui::OpenPopup("Recover Unsaved Work");
        openRestorePopup_ = false;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Recover Unsaved Work", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const gt::AutosaveRecord& rec = config.autosave;
    const std::string name = rec.name.empty() ? std::string("(unsaved project)") : rec.name;
    ImGui::TextUnformatted("BodeX closed unexpectedly with unsaved work.");
    ImGui::Spacing();
    ImGui::Text("Project:   %s", name.c_str());
    if (!rec.savedIso.empty())
        ImGui::Text("Autosaved: %s", rec.savedIso.c_str());
    ImGui::Spacing();

    if (ImGui::Button("Restore", ImVec2(110, 0))) {
        restoreFromAutosave();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(110, 0))) {
        clearAutosave();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
