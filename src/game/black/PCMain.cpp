#include "game/black/PCMain.h"

#include "core/Config.h"
#include "core/Hook.h"
#include "core/Log.h"
#include "core/Windows.h"

#include <array>
#include <cctype>

namespace black {

namespace {

/// Drive letter of the folder runblack.exe was started from (the install folder).
char InstallDriveLetter() noexcept
{
    std::array<char, MAX_PATH> directory{};
    const DWORD length = GetCurrentDirectoryA(static_cast<DWORD>(directory.size()), directory.data());
    if (length >= 2 && directory[1] == ':') {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(directory[0])));
    }
    return 'C';
}

}  // namespace

bool IsGameDisc(std::string_view volumeLabel, std::string_view fileSystem) noexcept
{
    return volumeLabel == kDiscVolumeLabel && fileSystem == kDiscFileSystem;
}

std::int32_t FindGameDisc() noexcept
{
    // No "insert disk" error boxes for empty drives.
    SetErrorMode(SEM_FAILCRITICALERRORS);

    const bool allDrives = bw::GetConfig().discFromInstallFolder;
    const DWORD drives = GetLogicalDrives();
    // The original probes 'd' .. 'y'.
    const char first = allDrives ? 'a' : 'd';
    const char last = allDrives ? 'z' : 'y';
    for (char letter = first; letter <= last; ++letter) {
        if (allDrives && (drives & (1u << (letter - 'a'))) == 0) {
            continue;
        }
        const std::array<char, 4> root{letter, ':', '\\', '\0'};
        std::array<char, 256> label{};
        std::array<char, 256> fileSystem{};
        DWORD serial = 0;
        DWORD maximumComponent = 0;
        DWORD flags = 0;
        if (!GetVolumeInformationA(root.data(), label.data(), static_cast<DWORD>(label.size()), &serial,
                                   &maximumComponent, &flags, fileSystem.data(),
                                   static_cast<DWORD>(fileSystem.size()))) {
            continue;
        }
        if (IsGameDisc(label.data(), fileSystem.data())) {
            g_discDriveLetter = letter;
            g_discFound = 1;
            bw::log::Info("Game disc found in {}:", letter);
            return 1;
        }
    }

    if (allDrives) {
        g_discDriveLetter = InstallDriveLetter();
        g_discFound = 1;
        bw::log::Info("No game disc; using the install folder ({}:)", g_discDriveLetter);
        return 1;
    }
    return 0;
}

BW_HOOK_FUNCTION(0x005f1250, FindGameDisc);

}  // namespace black
