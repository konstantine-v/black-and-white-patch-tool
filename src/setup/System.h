#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

/// What setup changes outside the game folder: the per-user uninstall entry, shortcuts and the game's GameDir value.
/// Nothing here needs administrator rights.
namespace bw::setup {

inline constexpr wchar_t kProductName[] = L"Black & White Decompilation";

/// The game folder of the previous installation, from its uninstall entry.
[[nodiscard]] std::optional<std::filesystem::path> RegisteredFolder();

/// Best guess for the game folder: a previous installation, the game's GameDir value, the folder setup runs from, the
/// retail default. Empty when none of them holds runblack.exe.
[[nodiscard]] std::filesystem::path FindGameFolder();

/// Adds or updates the entry in Settings > Apps (HKEY_CURRENT_USER, so no administrator rights).
void Register(const std::filesystem::path& folder, std::string_view version, std::uint64_t sizeBytes);
void Unregister();

/// Sets HKCU\Software\Lionhead Studios Ltd\Black & White\GameDir when the game has none: without it profiles and
/// saves go to \dev\black (docs/FIXES.md, "Settings and saves").
void EnsureGameDir(const std::filesystem::path& folder);

enum class ShortcutPlace { StartMenu, Desktop };

/// A shortcut to BWLaunch.exe in `folder`, with the game's icon. Throws SetupError.
void CreateShortcut(ShortcutPlace place, const std::filesystem::path& folder);
/// Removes the shortcuts that start BWLaunch.exe in `folder`.
void RemoveShortcuts(const std::filesystem::path& folder);

/// True while runblack.exe or BWLaunch.exe from `folder` is running.
[[nodiscard]] bool IsGameRunning(const std::filesystem::path& folder);

/// True when this process cannot write to `folder` (e.g. below Program Files without administrator rights).
[[nodiscard]] bool NeedsElevation(const std::filesystem::path& folder);

}  // namespace bw::setup
