#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <string_view>
#include <utility>

/// Bounded replacements for the sprintf/strcpy/strcat the original used on fixed-size C buffers. They truncate
/// instead of overflowing and always leave the buffer zero-terminated.
namespace bw::text {

/// Replaces the contents of `buffer` with `text`.
inline void CopyInto(char* buffer, std::size_t capacity, std::string_view text) noexcept
{
    if (buffer == nullptr || capacity == 0) {
        return;
    }
    const std::size_t count = std::min(text.size(), capacity - 1);
    std::memcpy(buffer, text.data(), count);
    buffer[count] = '\0';
}

/// Appends `text` to the zero-terminated contents of `buffer`.
inline void AppendInto(char* buffer, std::size_t capacity, std::string_view text) noexcept
{
    if (buffer == nullptr || capacity == 0) {
        return;
    }
    const std::size_t length = strnlen(buffer, capacity - 1);
    CopyInto(buffer + length, capacity - length, text);
}

/// Replaces the contents of `buffer` with the formatted text.
template <typename... Args>
void FormatInto(char* buffer, std::size_t capacity, std::format_string<Args...> format, Args&&... args) noexcept
{
    if (buffer == nullptr || capacity == 0) {
        return;
    }
    try {
        const auto result = std::format_to_n(buffer, static_cast<std::ptrdiff_t>(capacity - 1), format,
                                             std::forward<Args>(args)...);
        *result.out = '\0';
    }
    catch (...) {
        buffer[0] = '\0';
    }
}

/// Appends the formatted text to the zero-terminated contents of `buffer`.
template <typename... Args>
void AppendFormatInto(char* buffer, std::size_t capacity, std::format_string<Args...> format, Args&&... args) noexcept
{
    if (buffer == nullptr || capacity == 0) {
        return;
    }
    const std::size_t length = strnlen(buffer, capacity - 1);
    FormatInto(buffer + length, capacity - length, format, std::forward<Args>(args)...);
}

}  // namespace bw::text
