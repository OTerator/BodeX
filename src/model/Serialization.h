#pragma once

// JSON persistence for a Project (one .json file per project). All paths are
// UTF-8; file I/O goes through wide Win32 calls so Unicode paths work.

#include <string>
#include "model/Project.h"

namespace gt {

// Serialize to a pretty-printed JSON string.
std::string toJsonString(const Project& p, int indent = 2);

// Parse from a JSON string. Returns false and sets *err on malformed input.
// On success the project is passed through ensureShape().
bool projectFromJsonString(const std::string& text, Project& out, std::string* err = nullptr);

// File helpers (UTF-8 paths). Return false and set *err on failure.
bool saveProject(const std::string& path, const Project& p, std::string* err = nullptr);
bool loadProject(const std::string& path, Project& out, std::string* err = nullptr);

} // namespace gt
