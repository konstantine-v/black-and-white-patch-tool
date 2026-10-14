#pragma once

#include "core/Memory.h"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace bw {

/// User settings from BWDecomp.ini (next to BWDecomp.dll). Every fix can be switched off so that original behaviour
/// stays reproducible; see docs/FIXES.md for what each one changes.
struct Config {
    // [Fixes]
    bool dpiAware = true;                        ///< DpiAware: opt out of Windows DPI virtualisation.
    bool executableSelfModifyingCode = true;     ///< ExecutableSelfModifyingCode: SELFMOD section runnable under DEP.
    std::uint32_t maxReportedPhysicalMemoryMiB = 512;  ///< MaxReportedPhysicalMemoryMB: 0 = unlimited.
    std::uint32_t maxReportedVideoMemoryMiB = 1024;    ///< MaxReportedVideoMemoryMB: 0 = only clamp to INT32_MAX.

    // [Hooks]
    std::vector<Address> disabledHooks;          ///< Disabled: original addresses that keep running original code.

    // [Log]
    bool logDebug = false;                       ///< Debug: also log every installed hook.

    [[nodiscard]] static Config Load(const std::filesystem::path& iniFile);
};

/// Parses hexadecimal addresses separated by spaces, tabs or commas ("0x0045c610, 45d4a0"); invalid tokens are
/// skipped.
[[nodiscard]] std::vector<Address> ParseAddressList(std::wstring_view text);

/// The configuration loaded at start-up (defaults until bw::SetConfig is called).
[[nodiscard]] const Config& GetConfig() noexcept;
void SetConfig(Config config) noexcept;

}  // namespace bw
