#pragma once

#include "core/Windows.h"

#include <string>
#include <string_view>

namespace bw {

/// UTF-8 to UTF-16 for the wide Windows API. Invalid sequences become U+FFFD.
[[nodiscard]] inline std::wstring Widen(std::string_view utf8)
{
    if (utf8.empty()) {
        return {};
    }
    const int size = static_cast<int>(utf8.size());
    std::wstring wide(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, nullptr, 0)),
                      L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, wide.data(), static_cast<int>(wide.size()));
    return wide;
}

/// UTF-16 to UTF-8. Unpaired surrogates become U+FFFD.
[[nodiscard]] inline std::string Utf8(std::wstring_view wide)
{
    if (wide.empty()) {
        return {};
    }
    const int size = static_cast<int>(wide.size());
    std::string utf8(
        static_cast<std::size_t>(WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, nullptr, 0, nullptr, nullptr)),
        '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, utf8.data(), static_cast<int>(utf8.size()), nullptr, nullptr);
    return utf8;
}

}  // namespace bw
