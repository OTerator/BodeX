#include "ui/platform_dialogs.h"

#include "util/utf.h"   // windows.h (WIN32_LEAN_AND_MEAN + NOMINMAX)
#include <commdlg.h>
#include <cwchar>

namespace gt::ui {

namespace {

// The filter is a double-NUL-terminated list of NUL-separated pairs.
const wchar_t* kFilter = L"BodeX Project (*.json)\0*.json\0All Files (*.*)\0*.*\0";

bool runDialog(std::string& outPath, const std::string& initialDir,
               const std::string& suggestedName, bool save)
{
    wchar_t fileBuf[1024];
    fileBuf[0] = L'\0';
    if (!suggestedName.empty()) {
        std::wstring wn = utf8_to_wide(suggestedName);
        std::wcsncpy(fileBuf, wn.c_str(), 1023);
        fileBuf[1023] = L'\0';
    }

    std::wstring wInitDir = utf8_to_wide(initialDir);

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ::GetActiveWindow();
    ofn.lpstrFilter = kFilter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = 1024;
    ofn.lpstrInitialDir = wInitDir.empty() ? nullptr : wInitDir.c_str();
    ofn.lpstrDefExt = L"json";

    BOOL ok;
    if (save) {
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        ok = ::GetSaveFileNameW(&ofn);
    } else {
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        ok = ::GetOpenFileNameW(&ofn);
    }
    if (!ok)
        return false;

    outPath = wide_to_utf8(std::wstring(fileBuf));
    return true;
}

} // namespace

bool openProjectDialog(std::string& outPath, const std::string& initialDir)
{
    return runDialog(outPath, initialDir, "", /*save=*/false);
}

bool saveProjectDialog(std::string& outPath, const std::string& initialDir,
                       const std::string& suggestedName)
{
    return runDialog(outPath, initialDir, suggestedName, /*save=*/true);
}

} // namespace gt::ui
