#include "core/Log.h"

#include "core/Windows.h"

#include <algorithm>
#include <array>

namespace bw::log {

namespace {

HANDLE g_file = INVALID_HANDLE_VALUE;
bool g_debug = false;
SRWLOCK g_lock = SRWLOCK_INIT;

constexpr std::string_view LevelName(Level level) noexcept
{
    switch (level) {
    case Level::Debug: return "debug";
    case Level::Info: return "info ";
    case Level::Warning: return "warn ";
    case Level::Error: return "error";
    }
    return "?    ";
}

}  // namespace

void Open(const std::filesystem::path& file, bool debug) noexcept
{
    g_debug = debug;
    g_file = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool DebugEnabled() noexcept
{
    return g_debug;
}

std::string PathText(const std::filesystem::path& path) noexcept
{
    const std::wstring& wide = path.native();
    if (wide.empty()) {
        return {};
    }
    try {
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                             nullptr, nullptr);
        std::string text(static_cast<std::size_t>(std::max(size, 0)), char{});
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), text.data(), size, nullptr,
                            nullptr);
        return text;
    }
    catch (...) {
        return {};
    }
}

void Write(Level level, std::string_view message) noexcept
{
    if (level == Level::Debug && !g_debug) {
        return;
    }
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    // Short lines are assembled on the stack; longer ones are cut (the log is for people, not data).
    std::array<char, 1024> line;
    const auto end = std::format_to_n(line.data(), static_cast<std::ptrdiff_t>(line.size() - 2),
                                      "{:02}:{:02}:{:02}.{:03} {} {}", now.wHour, now.wMinute, now.wSecond,
                                      now.wMilliseconds, LevelName(level), message)
                         .out;
    char* cursor = end;
    *cursor++ = '\r';
    *cursor++ = '\n';

    AcquireSRWLockExclusive(&g_lock);
    DWORD written = 0;
    WriteFile(g_file, line.data(), static_cast<DWORD>(cursor - line.data()), &written, nullptr);
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace bw::log
