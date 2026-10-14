#pragma once

#include "core/Memory.h"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace bw {

/// [Display] WindowMode.
enum class WindowMode : std::uint8_t {
    FullScreen = 0,  ///< Exclusive full screen, as the original.
    Window = 1,      ///< A resizable window; the picture is scaled to it.
    Borderless = 2,  ///< A borderless window covering the monitor; the picture is scaled to it.
};

/// User settings from BWDecomp.ini (next to BWDecomp.dll). Every fix can be switched off so that original behaviour
/// stays reproducible; see docs/FIXES.md for what each one changes.
struct Config {
    // [Fixes]
    bool directXVersionReport = true;            ///< DirectXVersionReport: answer the DirectX 7 start-up check.
    bool discFromInstallFolder = true;           ///< DiscFromInstallFolder: play without the disc.
    bool promoteColorDepth16To32 = true;         ///< ColorDepth32: run 16-bit modes at 32 bits.
    bool displayModeFallback = true;             ///< DisplayModeFallback: step down when Direct3D refuses a mode.
    bool dpiAware = true;                        ///< DpiAware: opt out of Windows DPI virtualisation.
    bool executableSelfModifyingCode = true;     ///< ExecutableSelfModifyingCode: SELFMOD section runnable under DEP.
    bool closeRequestsQuit = true;               ///< CloseRequestsQuit: WM_CLOSE quits like Alt+F4.
    bool minimisedWait = true;                   ///< MinimisedWait: idle while minimised; quitting works.
    std::uint32_t shutdownTimeoutSeconds = 60;   ///< ShutdownTimeoutSeconds: end a stalled quit; 0 = never.
    std::uint32_t maxReportedPhysicalMemoryMiB = 512;  ///< MaxReportedPhysicalMemoryMB: 0 = unlimited.
    std::uint32_t maxReportedVideoMemoryMiB = 1024;    ///< MaxReportedVideoMemoryMB: 0 = only clamp to INT32_MAX.

    // [Display]
    WindowMode windowMode = WindowMode::FullScreen;  ///< WindowMode: 0 full screen, 1 window, 2 borderless.
    bool keepAspectRatio = true;                 ///< KeepAspectRatio: black bars instead of stretching.

    // [Hooks]
    std::vector<Address> disabledHooks;          ///< Disabled: original addresses that keep running original code.

    // [Diagnostics]
    bool traceExit = true;                       ///< TraceExit: log ExitProcess/PostQuitMessage/MessageBoxA callers.
    bool traceExceptions = true;                 ///< TraceExceptions: log exceptions with a stack scan.
    bool forwardDebugOutput = false;             ///< ForwardDebugOutput: copy the game's OutputDebugString to the log.
    std::uint32_t screenshotKey = 0;             ///< ScreenshotKey: virtual-key code that saves a screenshot; 0 = none.

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
