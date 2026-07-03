#include "ui/platform_dialogs.h"

#include "util/utf.h"   // windows.h (WIN32_LEAN_AND_MEAN + NOMINMAX)
#include <commdlg.h>
#include <cwchar>

namespace gt::ui {

namespace {

// Filters are double-NUL-terminated lists of NUL-separated pairs.
const wchar_t* kProjectFilter = L"BodeX Project (*.json)\0*.json\0All Files (*.*)\0*.*\0";
const wchar_t* kImageFilter   = L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0All Files (*.*)\0*.*\0";

bool runDialog(std::string& outPath, const std::string& initialDir,
               const std::string& suggestedName, bool save,
               const wchar_t* filter, const wchar_t* defExt)
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
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = 1024;
    ofn.lpstrInitialDir = wInitDir.empty() ? nullptr : wInitDir.c_str();
    ofn.lpstrDefExt = defExt;

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
    return runDialog(outPath, initialDir, "", /*save=*/false, kProjectFilter, L"json");
}

bool saveProjectDialog(std::string& outPath, const std::string& initialDir,
                       const std::string& suggestedName)
{
    return runDialog(outPath, initialDir, suggestedName, /*save=*/true, kProjectFilter, L"json");
}

bool openImageDialog(std::string& outPath, const std::string& initialDir)
{
    return runDialog(outPath, initialDir, "", /*save=*/false, kImageFilter, nullptr);
}

} // namespace gt::ui
