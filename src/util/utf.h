#pragma once

// UTF-8 <-> UTF-16 conversion helpers. The model/serialization layer stores all
// text (including file paths) as UTF-8 std::string; Win32 APIs need UTF-16.
// Header-only and inline so no extra translation unit needs linking (keeps the
// non-GUI test build simple).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

namespace gt {

inline std::wstring utf8_to_wide(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

inline std::string wide_to_utf8(const std::wstring& w)
{
    if (w.empty())
        return std::string();
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace gt
