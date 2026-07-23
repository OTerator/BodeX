#include "ui/SettingsScreen.h"

#include "app/App.h"
#include "ui/ProjectSettingsScreen.h"  // projectSettingsSection (Project tab)
#include "ui/widgets.h"                // bigTitle
#include "model/AppConfig.h"           // gt::saveConfig, gt::Preferences
#include "imgui.h"

#include <algorithm>
#include <cstdio>

namespace gt::ui {

namespace {

const ImVec4 kMuted(0.60f, 0.62f, 0.66f, 1.0f);

// Read-only shortcut reference shown in the Keybinds section (action, keys). Kept
// in sync with the grid handler / F1 overlay (gridShortcutsText, GradingTable.cpp).
struct KeyRow { const char* action; const char* keys; };
const KeyRow kKeybinds[] = {
    {"Move selection",            "Arrows"},
    {"Type awarded points",       "0-9 ."},
    {"Step points +/- (1 press)", "+ / -"},
    {"Open inline edit / page",   "Space (again = last page)"},
    {"Edit last page directly",   "p"},
    {"Full marks",                "f"},
    {"Open cell editor",          "e / F2"},
    {"Toggle No-submission (row)","n"},
    {"Clear cell",                "Del"},
    {"Commit (down / right)",     "Enter / Tab   (Esc cancels)"},
    {"Undo / Redo",               "Ctrl+Z / Ctrl+Y"},
    {"Save",                      "Ctrl+S"},
    {"New / Open project",        "Ctrl+N / Ctrl+O"},
    {"Paste clipboard image",     "Ctrl+V"},
    {"Paint full marks",          "Right-drag"},
    {"Zoom the grid",             "Ctrl+wheel"},
    {"Focus view <-> Grid",       "F3"},
    {"Shortcuts overlay",         "F1"},
    {"Note direction LTR / RTL",  "Ctrl+Left-Shift / Ctrl+Right-Shift"},
};

// Persist the config now (each control commits immediately; writes are tiny).
void persist(App& app) { gt::saveConfig(app.config); }

void generalSection(App& app)
{
    gt::Preferences& p = app.config.prefs;

    ImGui::SeparatorText("Appearance");

    ImGui::SetNextItemWidth(220);
    const char* themes[] = { "Dark", "Light", "Classic" };
    if (ImGui::Combo("Theme", &p.theme, themes, IM_ARRAYSIZE(themes))) {
        app.applyDisplaySettings();
        persist(app);
    }

    ImGui::SetNextItemWidth(220);
    if (ImGui::SliderFloat("UI scale", &p.uiScale, 0.5f, 3.0f, "%.2fx"))
        app.applyDisplaySettings();                 // live preview while dragging
    if (ImGui::IsItemDeactivatedAfterEdit())
        persist(app);
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "(menus/dialogs; grid cells use Ctrl+wheel zoom)");

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::SeparatorText("Grading");

    ImGui::SetNextItemWidth(160);
    if (ImGui::InputDouble("+/- point step", &p.stepSize, 0.25, 1.0, "%.2f")) {
        p.stepSize = std::clamp(p.stepSize, 0.05, 100.0);
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        persist(app);
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "step applied by +/- in the grid and cell editor");
    for (double preset : { 0.25, 0.5, 1.0, 2.0 }) {
        ImGui::SameLine();
        char lbl[16];
        std::snprintf(lbl, sizeof lbl, "%.2g", preset);
        if (ImGui::SmallButton(lbl)) { p.stepSize = preset; persist(app); }
    }

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::SeparatorText("Display / resolution");

    // Fullscreen toggle: applied to the Win32 window by the host after render.
    if (ImGui::Checkbox("Borderless fullscreen", &p.fullscreen)) {
        app.requestWindowChange();
        persist(app);
    }

    ImGui::BeginDisabled(p.fullscreen);
    ImGui::TextUnformatted("Window size:");
    ImGui::SameLine();
    struct Preset { const char* label; int w, h; };
    for (const Preset& pr : { Preset{"1280x720", 1280, 720},
                              Preset{"1600x900", 1600, 900},
                              Preset{"1920x1080", 1920, 1080} }) {
        if (ImGui::SmallButton(pr.label)) {
            p.winW = pr.w; p.winH = pr.h;
            app.requestWindowChange();
            persist(app);
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    int wh[2] = { p.winW, p.winH };
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputInt2("Custom W x H", wh)) {
        p.winW = std::clamp(wh[0], 640, 16384);
        p.winH = std::clamp(wh[1], 480, 16384);
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply size")) {
        app.requestWindowChange();
        persist(app);
    }
    ImGui::EndDisabled();

    // DPI / scale override: 0 = auto (monitor DPI). A checkbox seeds a sensible
    // starting factor so the slider isn't stuck at 0 when first enabled.
    bool dpiOn = p.dpiOverride > 0.0f;
    if (ImGui::Checkbox("Override display scale (DPI)", &dpiOn)) {
        p.dpiOverride = dpiOn ? 1.5f : 0.0f;
        app.applyDisplaySettings();
        persist(app);
    }
    if (dpiOn) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderFloat("##dpi", &p.dpiOverride, 0.75f, 3.0f, "%.2fx"))
            app.applyDisplaySettings();
        if (ImGui::IsItemDeactivatedAfterEdit())
            persist(app);
    } else {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "auto (monitor DPI)");
    }

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::SeparatorText("Data");

    ImGui::SetNextItemWidth(160);
    if (ImGui::InputDouble("Autosave interval (s)", &p.autosaveSec, 5.0, 30.0, "%.0f")) {
        p.autosaveSec = std::clamp(p.autosaveSec, 2.0, 3600.0);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        app.applyPrefsRuntime();
        persist(app);
    }
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "BODEX_AUTOSAVE_SEC env overrides this");

    ImGui::BeginDisabled(app.config.recentProjects.empty());
    if (ImGui::Button("Clear recent projects")) {
        app.config.recentProjects.clear();
        persist(app);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "%d in the Home list",
                       static_cast<int>(app.config.recentProjects.size()));
}

void keybindsSection()
{
    ImGui::SeparatorText("Keyboard shortcuts");
    ImGui::TextColored(kMuted, "Reference only (rebinding is planned). Grid keys act on the selected cell.");
    ImGui::Dummy(ImVec2(0, 4));

    if (ImGui::BeginTable("keybinds", 2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Action",   ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableHeadersRow();
        for (const KeyRow& r : kKeybinds) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r.action);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r.keys);
        }
        ImGui::EndTable();
    }
}

} // namespace

void settingsScreen(App& app)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Settings", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    bigTitle("Settings", 24.0f);
    ImGui::Dummy(ImVec2(0, 6));

    // Left category nav.
    ImGui::BeginChild("nav", ImVec2(190, -44), ImGuiChildFlags_Borders);
    auto navItem = [&](const char* label, App::SettingsSection s, bool enabled = true) {
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Selectable(label, app.settingsSection == s))
            app.openSettings(s);
        ImGui::EndDisabled();
    };
    navItem("General",  App::SettingsSection::General);
    navItem("Keybinds", App::SettingsSection::Keybinds);
    navItem("Project",  App::SettingsSection::Project, app.hasProject);
    ImGui::EndChild();

    // Right content pane.
    ImGui::SameLine();
    ImGui::BeginChild("content", ImVec2(0, -44), ImGuiChildFlags_Borders);
    switch (app.settingsSection) {
        case App::SettingsSection::General:  generalSection(app);          break;
        case App::SettingsSection::Keybinds: keybindsSection();            break;
        case App::SettingsSection::Project:  projectSettingsSection(app);  break;
    }
    ImGui::EndChild();

    // Close returns to where the user was (Project keeps its own Apply/Cancel).
    if (ImGui::Button("Close", ImVec2(120, 32))) {
        gt::saveConfig(app.config);
        app.screen = app.hasProject ? App::Screen::Grading : App::Screen::Home;
    }

    ImGui::End();
}

} // namespace gt::ui
