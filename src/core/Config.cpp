#include "core/Config.h"

#include "core/Windows.h"

#include <array>
#include <charconv>
#include <string>
#include <string_view>
#include <utility>

namespace bw {

namespace {

Config g_config;

std::uint32_t ReadUnsigned(const std::filesystem::path& ini, const wchar_t* section, const wchar_t* key,
                           std::uint32_t fallback)
{
    return GetPrivateProfileIntW(section, key, static_cast<INT>(fallback), ini.c_str());
}

bool ReadBool(const std::filesystem::path& ini, const wchar_t* section, const wchar_t* key, bool fallback)
{
    return ReadUnsigned(ini, section, key, fallback ? 1u : 0u) != 0;
}

}  // namespace

std::vector<Address> ParseAddressList(std::wstring_view text)
{
    std::vector<Address> addresses;
    std::string token;
    auto flush = [&] {
        std::string_view digits = token;
        if (digits.starts_with("0x") || digits.starts_with("0X")) {
            digits.remove_prefix(2);
        }
        Address value = 0;
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
        if (!digits.empty() && error == std::errc{} && end == digits.data() + digits.size()) {
            addresses.push_back(value);
        }
        token.clear();
    };
    for (const wchar_t c : text) {
        if (c == L' ' || c == L',' || c == L'\t') {
            flush();
        }
        else if (c < 0x80) {
            token.push_back(static_cast<char>(c));
        }
    }
    flush();
    return addresses;
}

Config Config::Load(const std::filesystem::path& iniFile)
{
    Config config;
    config.dpiAware = ReadBool(iniFile, L"Fixes", L"DpiAware", config.dpiAware);
    config.executableSelfModifyingCode =
        ReadBool(iniFile, L"Fixes", L"ExecutableSelfModifyingCode", config.executableSelfModifyingCode);
    config.maxReportedPhysicalMemoryMiB =
        ReadUnsigned(iniFile, L"Fixes", L"MaxReportedPhysicalMemoryMB", config.maxReportedPhysicalMemoryMiB);
    config.maxReportedVideoMemoryMiB =
        ReadUnsigned(iniFile, L"Fixes", L"MaxReportedVideoMemoryMB", config.maxReportedVideoMemoryMiB);
    config.logDebug = ReadBool(iniFile, L"Log", L"Debug", config.logDebug);

    std::array<wchar_t, 4096> disabled{};
    GetPrivateProfileStringW(L"Hooks", L"Disabled", L"", disabled.data(), static_cast<DWORD>(disabled.size()),
                             iniFile.c_str());
    config.disabledHooks = ParseAddressList(disabled.data());
    return config;
}

const Config& GetConfig() noexcept
{
    return g_config;
}

void SetConfig(Config config) noexcept
{
    g_config = std::move(config);
}

}  // namespace bw
