#pragma once

#include "core/Config.h"
#include "core/Windows.h"

/// Compatibility fixes that are not (yet) part of a decompiled function: process settings and import patches.
/// Fixes inside decompiled code (e.g. the video-memory overflow in LHScreen) live with that code instead.
/// Each fix is documented in docs/FIXES.md and can be disabled in BWDecomp.ini.
namespace bw::fixes {

/// Applies every enabled fix. Runs from DllMain before the game's entry point, so it must stay loader-lock safe.
void InstallAll(HMODULE game, const Config& config) noexcept;

void InstallDpiAwareness() noexcept;
void InstallExecutableSelfModifyingCode(HMODULE game) noexcept;
void InstallDirectXVersionReport(HMODULE game) noexcept;
void InstallPhysicalMemoryReport(HMODULE game, std::uint32_t maxMiB) noexcept;

/// Clamps a MEMORYSTATUS so that dwTotalPhys <= maxMiB (0 = unlimited) and the available counts never exceed the
/// totals.
void ClampMemoryStatus(MEMORYSTATUS& status, std::uint32_t maxMiB) noexcept;

}  // namespace bw::fixes
