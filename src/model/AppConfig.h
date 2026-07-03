#pragma once

// Small persistent app configuration stored under %APPDATA%\BodeX.
// Currently just the recent-projects list that powers "resume an existing
// project" on the Home screen. All paths are UTF-8.

#include <string>
#include <vector>

namespace gt {

struct AppConfig {
    std::vector<std::string> recentProjects; // most-recent first, UTF-8 paths
};

// Directory helpers. Each creates the directory if it does not exist and
// returns a UTF-8 path (empty string on failure).
std::string appDataDir();     // %APPDATA%\BodeX
std::string projectsDir();    // %APPDATA%\BodeX\projects
std::string configFilePath(); // %APPDATA%\BodeX\config.json

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

} // namespace gt
