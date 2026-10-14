#include "fixes/Fixes.h"

#include "core/Image.h"
#include "core/Log.h"
#include "core/Patch.h"

#include <algorithm>

namespace bw::fixes {

namespace {

decltype(&::GlobalMemoryStatus) g_originalGlobalMemoryStatus = nullptr;
std::uint32_t g_maxPhysicalMemoryMiB = 0;

/// DSETUP.dll ordinal 11, DirectXSetupGetVersion.
using DirectXSetupGetVersionFunction = INT(WINAPI*)(DWORD* version, DWORD* revision);
constexpr std::uint16_t kDirectXSetupGetVersionOrdinal = 11;

/// DirectX 9.0c ("4.09.00.0904"), the version current Windows registers for compatibility.
INT WINAPI DirectXSetupGetVersionCurrent(DWORD* version, DWORD* revision)
{
    if (version != nullptr) {
        *version = 0x0004'0009;
    }
    if (revision != nullptr) {
        *revision = 0x0000'0904;
    }
    return TRUE;
}

void WINAPI GlobalMemoryStatusClamped(MEMORYSTATUS* status)
{
    g_originalGlobalMemoryStatus(status);
    ClampMemoryStatus(*status, g_maxPhysicalMemoryMiB);
}

}  // namespace

void InstallAll(HMODULE game, const Config& config) noexcept
{
    if (config.directXVersionReport) {
        InstallDirectXVersionReport(game);
    }
    if (config.dpiAware) {
        InstallDpiAwareness();
    }
    if (config.executableSelfModifyingCode) {
        InstallExecutableSelfModifyingCode(game);
    }
    if (config.maxReportedPhysicalMemoryMiB != 0) {
        InstallPhysicalMemoryReport(game, config.maxReportedPhysicalMemoryMiB);
    }
}

void InstallDpiAwareness() noexcept
{
    // Without this, Windows scales the game on high-DPI displays: GetSystemMetrics reports the scaled desktop, so
    // LHScreen::SetMode (0x00748de0) sizes the full-screen window smaller than the mode it sets, and windowed mode is
    // bitmap-stretched. Must happen before the first window is created; nothing has run yet at this point.
    using SetContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto setContext =
        user32 != nullptr ? reinterpret_cast<SetContext>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))
                          : nullptr;
    if (setContext != nullptr && setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        log::Info("Fix DpiAware: per-monitor DPI awareness v2");
        return;
    }
    if (SetProcessDPIAware()) {
        log::Info("Fix DpiAware: system DPI awareness");
        return;
    }
    log::Warning("Fix DpiAware: could not change DPI awareness ({})", GetLastError());
}

void InstallDirectXVersionReport(HMODULE game) noexcept
{
    // GameMain (0x005f1550) calls DirectXSetupGetVersion and quits with "You need DirectX 7 or greater to play
    // Black & White." unless the version exceeds 0x00040006. The disc's 1999 DSETUP.dll derives the version from
    // system file versions and does not recognise current Windows, so the game never starts.
    const auto original = patch::ReplaceImport(game, "DSETUP.dll", kDirectXSetupGetVersionOrdinal,
                                               DirectXSetupGetVersionFunction{&DirectXSetupGetVersionCurrent});
    if (original == nullptr) {
        log::Warning("Fix DirectXVersionReport: DSETUP.dll ordinal 11 is not imported");
        return;
    }
    log::Info("Fix DirectXVersionReport: DirectX reported as 9.0c");
}

void InstallExecutableSelfModifyingCode(HMODULE game) noexcept
{
    // The software rasteriser patches immediates inside its own code (e.g. 0x00ee0285 stores the surface pitch into
    // four instructions). That code lives in the SELFMOD section, which is flagged read/write data, not executable,
    // so it faults whenever DEP is enforced for the process (DEP "AlwaysOn" or "OptOut" system policies).
    const std::optional<image::Section> selfmod = image::FindSection(game, "SELFMOD");
    if (!selfmod) {
        log::Warning("Fix ExecutableSelfModifyingCode: SELFMOD section not found");
        return;
    }
    if (!patch::SetProtection(selfmod->begin, selfmod->Size(), patch::Protection::ReadWriteExecute)) {
        log::Warning("Fix ExecutableSelfModifyingCode: VirtualProtect failed ({})", GetLastError());
        return;
    }
    log::Info("Fix ExecutableSelfModifyingCode: SELFMOD 0x{:08x}-0x{:08x} is executable", selfmod->begin,
              selfmod->end);
}

void InstallPhysicalMemoryReport(HMODULE game, std::uint32_t maxMiB) noexcept
{
    // GAudio's constructor (0x00420f80, Audio.cpp) gives the audio system a sample cache of dwTotalPhys / 8: 16-32 MB
    // on the 128-256 MB machines of 2001, but 256-512 MB of the 2 GB address space on a current PC, which starves
    // the rest of the game. It is the image's only GlobalMemoryStatus call.
    g_maxPhysicalMemoryMiB = maxMiB;
    g_originalGlobalMemoryStatus =
        patch::ReplaceImport(game, "KERNEL32.dll", "GlobalMemoryStatus", &GlobalMemoryStatusClamped);
    if (g_originalGlobalMemoryStatus == nullptr) {
        log::Warning("Fix MaxReportedPhysicalMemoryMB: GlobalMemoryStatus import not found");
        return;
    }
    log::Info("Fix MaxReportedPhysicalMemoryMB: physical memory reported as at most {} MB", maxMiB);
}

void ClampMemoryStatus(MEMORYSTATUS& status, std::uint32_t maxMiB) noexcept
{
    if (maxMiB != 0) {
        const std::uint64_t limit = std::uint64_t{maxMiB} << 20;
        if (status.dwTotalPhys > limit) {
            status.dwTotalPhys = static_cast<SIZE_T>(limit);
        }
    }
    status.dwAvailPhys = std::min(status.dwAvailPhys, status.dwTotalPhys);
}

}  // namespace bw::fixes
