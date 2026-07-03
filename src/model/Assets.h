#pragma once

// Image asset storage: attached images are copied into a folder beside the
// project's .json (e.g. Exam1.assets/) so they travel with the project. Before a
// project is first saved, its images live in a per-project staging folder under
// %APPDATA%\BodeX\staging\<id>. GUI-free (Win32 file ops only), like AppConfig.

#include <string>
#include <vector>
#include "model/Project.h"

namespace gt {

// dir(projectPath)/stem(projectPath).assets — created on demand. "" if no path.
std::string projectAssetsDir(const std::string& projectPath);

// %APPDATA%\BodeX\staging\<projectId> — created on demand.
std::string stagingAssetsDir(const std::string& projectId);

// Where the project's image files currently live: project assets if saved, else
// the staging dir.
std::string liveAssetsDir(const Project& p, const std::string& projectPath);

// Copy srcPath into destDir under a fresh unique filename; returns the stored
// filename (e.g. "img-ab12...png"), or "" on failure.
std::string importImage(const std::string& srcPath, const std::string& destDir);

// Ensure each listed filename exists in newDir, copying from fromDir when
// missing (used to migrate assets on Save / Save As).
void syncImages(const std::string& fromDir, const std::string& newDir,
                const std::vector<std::string>& files);

} // namespace gt
