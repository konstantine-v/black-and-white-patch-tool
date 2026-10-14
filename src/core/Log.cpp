#include "core/Log.h"

#include "core/Windows.h"

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

void WriteRaw(std::string_view text) noexcept
{
    DWORD written = 0;
    WriteFile(g_file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
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
    std::array<char, 32> stamp{};
    const auto stampEnd = std::format_to_n(stamp.data(), stamp.size(), "{:02}:{:02}:{:02}.{:03} ", now.wHour,
                                           now.wMinute, now.wSecond, now.wMilliseconds).out;

    AcquireSRWLockExclusive(&g_lock);
    WriteRaw({stamp.data(), static_cast<std::size_t>(stampEnd - stamp.data())});
    WriteRaw(LevelName(level));
    WriteRaw(" ");
    WriteRaw(message);
    WriteRaw("\r\n");
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace bw::log
