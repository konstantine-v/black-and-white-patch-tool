#pragma once

#include <string>
#include <string_view>

namespace bw {

/// Quotes one argument for a Windows command line so that CommandLineToArgvW (and the MSVC runtime) reads it back
/// unchanged: backslashes are doubled before a quote and at the end, quotes are escaped.
[[nodiscard]] inline std::wstring QuoteArgument(std::wstring_view argument)
{
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
        }
        else {
            quoted.append(backslashes, L'\\');
        }
        backslashes = 0;
        quoted += c;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L'"';
    return quoted;
}

}  // namespace bw
