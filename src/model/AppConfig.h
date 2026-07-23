#pragma once

// Small persistent app configuration stored under %APPDATA%\BodeX.
// Currently just the recent-projects list that powers "resume an existing
// project" on the Home screen. All paths are UTF-8.

#include <string>
#include <vector>

namespace gt {

// Describes the single pending crash-recovery autosave (the app has one project
// open at a time). Written under %APPDATA%\BodeX\autosave and pointed at by the
// config; deleted on every clean exit, so if it survives to the next launch the
// previous session did not close cleanly and we offer to recover it.
struct AutosaveRecord {
    std::string file;        // the .autosave path ("" = no pending autosave)
    std::string projectPath; // original .json ("" if never saved) — where Restore's project belongs
    std::string name;        // project name, for the recovery prompt
    std::string savedIso;    // last autosave time (nowIso())
    bool empty() const { return file.empty(); }
};

// Persistent user preferences (the Settings panel). All app-wide, saved in
// config.json under "prefs". Tolerant of missing keys (each falls back to the
// default below), so old configs load fine and new keys are additive.
struct Preferences {
    int    theme       = 0;      // 0 Dark, 1 Light, 2 Classic (ImGui::StyleColors*)
    double stepSize    = 1.0;    // +/- point step (gt::stepAwarded delta)
    float  uiScale     = 1.0f;   // user font/UI multiplier, on top of monitor DPI
    double autosaveSec = 30.0;   // autosave interval (BODEX_AUTOSAVE_SEC still wins)
    int    winW        = 1280;   // window size (preset or custom), applied at launch
    int    winH        = 820;
    bool   fullscreen  = false;  // borderless fullscreen vs normal window
    float  dpiOverride = 0.0f;   // 0 = auto (monitor DPI), else an explicit factor
};

struct AppConfig {
    std::vector<std::string> recentProjects; // most-recent first, UTF-8 paths
    AutosaveRecord           autosave;       // pending crash-recovery autosave (empty = none)
    Preferences              prefs;          // Settings-panel preferences
};

// Directory helpers. Each creates the directory if it does not exist and
// returns a UTF-8 path (empty string on failure).
std::string appDataDir();     // %APPDATA%\BodeX
std::string projectsDir();    // %APPDATA%\BodeX\projects
std::string autosaveDir();    // %APPDATA%\BodeX\autosave
std::string configFilePath(); // %APPDATA%\BodeX\config.json

// Config <-> JSON string (pure; no file I/O, so unit-testable). configFromJsonString
// is tolerant of missing/extra keys and always returns a usable config.
std::string configToJsonString(const AppConfig& cfg);
bool        configFromJsonString(const std::string& text, AppConfig& out);

// Load/save the config file (tolerant of a missing/corrupt file -> defaults).
AppConfig loadConfig();
void      saveConfig(const AppConfig& cfg);

// Move `path` to the front of the recent list (dedup, case-insensitive on
// Windows) and cap the list length.
void addRecentProject(AppConfig& cfg, const std::string& path);

// "YYYY-MM-DD HH:MM" local time, for Project::createdIso.
std::string nowIso();

// True if a file exists at the given UTF-8 path.
bool fileExists(const std::string& path);

// Delete the file at the given UTF-8 path. Returns true on success; a missing
// file is not treated as an error by callers (best-effort cleanup).
bool removeFile(const std::string& path);

} // namespace gt
