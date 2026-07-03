#pragma once

// Native Win32 open/save file dialogs (commdlg). Paths are UTF-8. Each returns
// false if the user cancelled.

#include <string>

namespace gt::ui {

bool openProjectDialog(std::string& outPathUtf8, const std::string& initialDirUtf8);
bool saveProjectDialog(std::string& outPathUtf8, const std::string& initialDirUtf8,
                       const std::string& suggestedNameUtf8);
bool openImageDialog(std::string& outPathUtf8, const std::string& initialDirUtf8);

} // namespace gt::ui
