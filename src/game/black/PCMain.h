#pragma once

// PC start-up and main loop (C:\dev\black\PCMain.cpp).

#include "core/Memory.h"

#include <cstdint>
#include <string_view>

namespace black {

/// Drive letter of the game disc. File opens fall back to "<letter>:\<path>" when a file is missing from the
/// install folder (LHReleasedFile, GAudio, ...). Initially 'D'.
BW_GLOBAL(char, g_discDriveLetter, 0x00b4dee8);
/// Set to 1 once FindGameDisc succeeded.
BW_GLOBAL(std::uint8_t, g_discFound, 0x00c67864);

/// Volume label and file system of the Black & White disc.
inline constexpr std::string_view kDiscVolumeLabel = "BNW";
inline constexpr std::string_view kDiscFileSystem = "CDFS";

/// Looks for the game disc (a real disc or a mounted image) and records its drive letter. GameMain (0x005f1550)
/// retries once a second and quits after 30 seconds when this keeps returning 0.
///
/// Original: probes D: to Y: only. Fix (DiscFromInstallFolder): probes every drive and, when no disc is present,
/// accepts the install folder, which install_game.py fills with everything the disc provides.
[[nodiscard]] std::int32_t FindGameDisc() noexcept;  // 0x005f1250

/// True for the volume label and file-system name of the Black & White disc.
[[nodiscard]] bool IsGameDisc(std::string_view volumeLabel, std::string_view fileSystem) noexcept;

}  // namespace black
