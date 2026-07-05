#include "model/AppConfig.h"

#include "util/utf.h"   // brings in windows.h with WIN32_LEAN_AND_MEAN + NOMINMAX
#include <shlobj.h>
#include "json.hpp"

#include <cstdio>
#include <ctime>
#include <algorithm>

namespace gt {

using nlohmann::json;

namespace {

constexpr size_t kMaxRecent = 12;

// %APPDATA%\BodeX as a wide path, created on demand. Empty on failure.
std::wstring appDataBaseW()
{
    wchar_t path[MAX_PATH];
    if (::SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path) != S_OK)
        return std::wstring();
    std::wstring dir = std::wstring(path) + L"\\BodeX";
    ::CreateDirectoryW(dir.c_str(), nullptr); // ignore ERROR_ALREADY_EXISTS
    return dir;
}

bool writeFile(const std::wstring& wpath, const std::string& data)
{
    FILE* f = ::_wfopen(wpath.c_str(), L"wb");
    if (!f)
        return false;
    if (!data.empty())
        std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
    return true;
}

bool readFile(const std::wstring& wpath, std::string& out)
{
    FILE* f = ::_wfopen(wpath.c_str(), L"rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0) { std::fclose(f); return false; }
    out.resize(static_cast<size_t>(sz));
    size_t got = sz > 0 ? std::fread(out.data(), 1, static_cast<size_t>(sz), f) : 0;
    std::fclose(f);
    out.resize(got);
    return true;
}

// Normalize separators so dedup treats / and \ the same.
std::string normalizeForCompare(std::string s)
{
    for (char& c : s)
        if (c == '/') c = '\\';
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
}

} // namespace

std::string appDataDir()
{
    return wide_to_utf8(appDataBaseW());
}

std::string projectsDir()
{
    std::wstring base = appDataBaseW();
    if (base.empty())
        return std::string();
    std::wstring dir = base + L"\\projects";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return wide_to_utf8(dir);
}

std::string autosaveDir()
{
    std::wstring base = appDataBaseW();
    if (base.empty())
        return std::string();
    std::wstring dir = base + L"\\autosave";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return wide_to_utf8(dir);
}

std::string configFilePath()
{
    std::wstring base = appDataBaseW();
    if (base.empty())
        return std::string();
    return wide_to_utf8(base + L"\\config.json");
}

bool fileExists(const std::string& path)
{
    if (path.empty())
        return false;
    DWORD attrs = ::GetFileAttributesW(utf8_to_wide(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool removeFile(const std::string& path)
{
    if (path.empty())
        return false;
    return ::DeleteFileW(utf8_to_wide(path).c_str()) != 0;
}

std::string configToJsonString(const AppConfig& cfg)
{
    json root;
    root["recentProjects"] = cfg.recentProjects;
    if (!cfg.autosave.empty()) {
        root["autosave"] = {
            {"file",        cfg.autosave.file},
            {"projectPath", cfg.autosave.projectPath},
            {"name",        cfg.autosave.name},
            {"savedIso",    cfg.autosave.savedIso},
        };
    }
    return root.dump(2);
}

bool configFromJsonString(const std::string& text, AppConfig& out)
{
    out = AppConfig{};
    try {
        json root = json::parse(text);
        if (root.contains("recentProjects") && root.at("recentProjects").is_array()) {
            for (const auto& jp : root.at("recentProjects")) {
                if (jp.is_string())
                    out.recentProjects.push_back(jp.get<std::string>());
            }
        }
        if (root.contains("autosave") && root.at("autosave").is_object()) {
            const json& a = root.at("autosave");
            auto str = [&](const char* k) -> std::string {
                return (a.contains(k) && a.at(k).is_string()) ? a.at(k).get<std::string>()
                                                              : std::string();
            };
            out.autosave.file        = str("file");
            out.autosave.projectPath = str("projectPath");
            out.autosave.name        = str("name");
            out.autosave.savedIso    = str("savedIso");
        }
    } catch (const std::exception&) {
        out = AppConfig{}; // corrupt config -> start clean
        return false;
    }
    return true;
}

AppConfig loadConfig()
{
    AppConfig cfg;
    std::string cfgPath = configFilePath();
    if (cfgPath.empty())
        return cfg;

    std::string text;
    if (!readFile(utf8_to_wide(cfgPath), text) || text.empty())
        return cfg;

    configFromJsonString(text, cfg); // tolerant; leaves cfg default on parse error

    // Drop recent entries whose file has since moved/been deleted.
    auto& v = cfg.recentProjects;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const std::string& p) { return !fileExists(p); }),
            v.end());
    // A pending autosave whose file is gone can no longer be recovered -> forget it.
    if (!cfg.autosave.empty() && !fileExists(cfg.autosave.file))
        cfg.autosave = AutosaveRecord{};

    return cfg;
}

void saveConfig(const AppConfig& cfg)
{
    std::string cfgPath = configFilePath();
    if (cfgPath.empty())
        return;
    writeFile(utf8_to_wide(cfgPath), configToJsonString(cfg));
}

void addRecentProject(AppConfig& cfg, const std::string& path)
{
    if (path.empty())
        return;
    // Copy up front in case `path` aliases an element of cfg.recentProjects
    // (erase/insert below would otherwise invalidate the reference).
    const std::string p = path;
    const std::string key = normalizeForCompare(p);
    auto& v = cfg.recentProjects;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const std::string& e) { return normalizeForCompare(e) == key; }),
            v.end());
    v.insert(v.begin(), p);
    if (v.size() > kMaxRecent)
        v.resize(kMaxRecent);
}

std::string nowIso()
{
    std::time_t t = std::time(nullptr);
    std::tm* lt = std::localtime(&t);
    if (!lt)
        return std::string();
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", lt);
    return std::string(buf);
}

} // namespace gt
