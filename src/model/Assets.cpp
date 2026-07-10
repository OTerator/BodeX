#include "model/Assets.h"

#include "model/AppConfig.h"  // appDataDir()
#include "util/utf.h"         // windows.h + UTF-8<->UTF-16

#include <shellapi.h>         // DragQueryFileW (CF_HDROP)
#include <cctype>
#include <cstdio>

namespace gt {

namespace {

std::string dirOf(const std::string& p)
{
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string() : p.substr(0, s);
}
std::string fileOf(const std::string& p)
{
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}
std::string stemOf(const std::string& file)
{
    size_t d = file.find_last_of('.');
    return d == std::string::npos ? file : file.substr(0, d);
}
std::string extOf(const std::string& p)
{
    size_t d = p.find_last_of('.');
    if (d == std::string::npos)
        return std::string();
    std::string e = p.substr(d);
    for (char& c : e)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}
void ensureDir(const std::string& utf8)
{
    if (!utf8.empty())
        ::CreateDirectoryW(utf8_to_wide(utf8).c_str(), nullptr); // ignore ERROR_ALREADY_EXISTS
}
bool fileThere(const std::string& utf8)
{
    return ::GetFileAttributesW(utf8_to_wide(utf8).c_str()) != INVALID_FILE_ATTRIBUTES;
}
bool isImageExt(const std::string& ext)
{
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
           ext == ".gif" || ext == ".tga"  || ext == ".psd"  || ext == ".webp";
}

} // namespace

std::string projectAssetsDir(const std::string& projectPath)
{
    if (projectPath.empty())
        return std::string();
    const std::string dir  = dirOf(projectPath);
    const std::string stem = stemOf(fileOf(projectPath));
    const std::string assets = (dir.empty() ? stem : dir + "/" + stem) + ".assets";
    ensureDir(assets);
    return assets;
}

std::string stagingAssetsDir(const std::string& projectId)
{
    const std::string base = appDataDir(); // %APPDATA%\BodeX (created)
    if (base.empty())
        return std::string();
    const std::string staging = base + "\\staging";
    ensureDir(staging);
    const std::string dir = staging + "\\" + (projectId.empty() ? "default" : projectId);
    ensureDir(dir);
    return dir;
}

std::string liveAssetsDir(const Project& p, const std::string& projectPath)
{
    return projectPath.empty() ? stagingAssetsDir(p.id) : projectAssetsDir(projectPath);
}

std::string importImage(const std::string& srcPath, const std::string& destDir)
{
    if (srcPath.empty() || destDir.empty())
        return std::string();
    ensureDir(destDir);
    std::string ext = extOf(srcPath);
    if (ext.empty())
        ext = ".png";
    const std::string name = "img-" + newProjectId() + ext; // reuse the 16-hex id generator
    const std::string dest = destDir + "/" + name;
    if (!::CopyFileW(utf8_to_wide(srcPath).c_str(), utf8_to_wide(dest).c_str(), TRUE))
        return std::string();
    return name;
}

void syncImages(const std::string& fromDir, const std::string& newDir,
                const std::vector<std::string>& files)
{
    if (newDir.empty())
        return;
    ensureDir(newDir);
    if (fromDir.empty() || fromDir == newDir)
        return;
    for (const std::string& f : files) {
        if (f.empty())
            continue;
        const std::string dest = newDir + "/" + f;
        if (fileThere(dest))
            continue;
        const std::string src = fromDir + "/" + f;
        ::CopyFileW(utf8_to_wide(src).c_str(), utf8_to_wide(dest).c_str(), FALSE);
    }
}

std::string clipboardImageToTempFile(const std::string& tempDir)
{
    if (tempDir.empty() || !::OpenClipboard(nullptr))
        return std::string();

    std::string result;

    // Prefer a bitmap (screenshots / snips). Windows synthesizes CF_DIB from
    // CF_DIBV5, so requesting CF_DIB gives a plain BITMAPINFOHEADER form.
    if (HANDLE h = ::GetClipboardData(CF_DIB)) {
        if (const void* p = ::GlobalLock(h)) {
            const SIZE_T n = ::GlobalSize(h);
            std::vector<uint8_t> bmp;
            if (n > 0 &&
                buildBmpFromDib(static_cast<const uint8_t*>(p), static_cast<size_t>(n), bmp)) {
                ensureDir(tempDir);
                const std::string dest = tempDir + "/clip_paste.bmp";
                if (FILE* f = ::_wfopen(utf8_to_wide(dest).c_str(), L"wb")) {
                    const size_t wrote = std::fwrite(bmp.data(), 1, bmp.size(), f);
                    std::fclose(f);
                    if (wrote == bmp.size())
                        result = dest;
                }
            }
            ::GlobalUnlock(h);
        }
    }

    // Otherwise accept a copied image file (Explorer "Copy" -> CF_HDROP).
    if (result.empty()) {
        if (HANDLE h = ::GetClipboardData(CF_HDROP)) {
            HDROP drop = static_cast<HDROP>(h);
            if (::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0) > 0) {
                UINT len = ::DragQueryFileW(drop, 0, nullptr, 0);
                std::wstring w(len, L'\0');
                ::DragQueryFileW(drop, 0, w.data(), len + 1);
                const std::string path = wide_to_utf8(w);
                if (isImageExt(extOf(path)))
                    result = path;
            }
        }
    }

    ::CloseClipboard();
    return result;
}

} // namespace gt
