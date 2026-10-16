#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

/// Recognising the runblack.exe on disk, for the launcher and the setup program (inside the game,
/// image::IsSupportedGame checks the mapped image instead).
namespace bw {

enum class GameBuild {
    Missing,         // no such file
    Unreadable,      // exists but cannot be read (or is implausibly large)
    Supported,       // image::kSupportedFileSha256
    RetailSafeDisc,  // image::kRetailSafeDiscFileSha256: same build, cannot start on current Windows
    Unknown,         // another version or language
};

struct GameExecutableCheck {
    GameBuild build = GameBuild::Missing;
    std::string sha256;  // lower-case hex; empty for Missing and Unreadable
};

/// SHA-256 of a whole file in lower-case hex; nullopt when it cannot be read or is larger than `largest` bytes.
[[nodiscard]] std::optional<std::string> FileSha256(const std::filesystem::path& file, std::uintmax_t largest);

[[nodiscard]] GameExecutableCheck CheckGameExecutable(const std::filesystem::path& file);

/// Why `file` cannot be started, for a message box; empty for the supported build.
[[nodiscard]] std::wstring DescribeGameExecutable(const GameExecutableCheck& check,
                                                  const std::filesystem::path& file);

}  // namespace bw
