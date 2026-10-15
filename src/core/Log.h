#pragma once

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

/// Plain-text log (BWDecomp.log next to the game). Formatting failures never propagate into game code.
namespace bw::log {

enum class Level { Debug, Info, Warning, Error };

void Open(const std::filesystem::path& file, bool debug) noexcept;
void Write(Level level, std::string_view message) noexcept;
/// True when Debug messages are written ([Log] Debug=1); lets callers skip work for them.
[[nodiscard]] bool DebugEnabled() noexcept;

/// `path` as UTF-8 for messages. Unlike path::string() it never throws (that one fails for names outside the ANSI
/// code page); empty when out of memory.
[[nodiscard]] std::string PathText(const std::filesystem::path& path) noexcept;

template <typename... Args>
void Format(Level level, std::format_string<Args...> format, Args&&... args) noexcept
{
    try {
        Write(level, std::format(format, std::forward<Args>(args)...));
    }
    catch (...) {
        Write(level, format.get());
    }
}

template <typename... Args>
void Debug(std::format_string<Args...> format, Args&&... args) noexcept
{
    Format(Level::Debug, format, std::forward<Args>(args)...);
}

template <typename... Args>
void Info(std::format_string<Args...> format, Args&&... args) noexcept
{
    Format(Level::Info, format, std::forward<Args>(args)...);
}

template <typename... Args>
void Warning(std::format_string<Args...> format, Args&&... args) noexcept
{
    Format(Level::Warning, format, std::forward<Args>(args)...);
}

template <typename... Args>
void Error(std::format_string<Args...> format, Args&&... args) noexcept
{
    Format(Level::Error, format, std::forward<Args>(args)...);
}

}  // namespace bw::log
