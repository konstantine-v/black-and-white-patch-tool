#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// Copying the patch into a game folder and removing it again. Every file written is listed, with its hash,
/// in a manifest, so removal deletes exactly what was installed and keeps what the player changed.
namespace bw::setup {

/// Folder below the game folder for the documentation, the uninstaller and the manifest.
inline constexpr std::wstring_view kDataFolder = L"BWDecomp";
inline constexpr std::wstring_view kManifestPath = L"BWDecomp\\Installed.txt";
inline constexpr std::wstring_view kUninstallerPath = L"BWDecomp\\Uninstall.exe";
inline constexpr std::wstring_view kLauncherPath = L"BWLaunch.exe";
/// Written by BWDecomp.dll, not by setup; removed with it.
inline constexpr std::wstring_view kGeneratedFiles[] = {L"BWDecomp.log", L"BWDecomp.directx.csv"};

/// What to do with a file the game folder already has.
enum class Existing {
    Replace,  // program files and documentation
    Keep,     // the player's settings: installed only when missing, removed only when unchanged
};

struct PayloadFile {
    std::wstring path;  // relative to the game folder
    std::span<const std::byte> data;
    Existing existing = Existing::Replace;
};

struct ManifestEntry {
    std::wstring path;
    std::string sha256;
    Existing existing = Existing::Replace;
};

struct Manifest {
    std::string version;  // empty when the manifest does not say
    std::vector<ManifestEntry> entries;
};

/// A failure to report to the player.
class SetupError : public std::exception {
public:
    explicit SetupError(std::wstring message) : m_message(std::move(message)) {}
    [[nodiscard]] const std::wstring& Message() const noexcept { return m_message; }
    [[nodiscard]] const char* what() const noexcept override { return "setup error"; }

private:
    std::wstring m_message;
};

/// True when `a` and `b` name the same file: lexically normalised and compared without regard to case, as Windows
/// compares file names.
[[nodiscard]] bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b) noexcept;

/// The system's text for a Windows error code, e.g. "Access is denied. (5)".
[[nodiscard]] std::wstring WindowsErrorText(unsigned long error);

/// True for a relative path that stays inside the folder it is relative to: no root, drive, stream, "." or ".."
/// component, or character Windows does not allow in file names. Manifests are read from disk, so their paths are
/// checked before anything is deleted.
[[nodiscard]] bool IsSafeRelativePath(std::wstring_view path) noexcept;

[[nodiscard]] std::string Sha256Hex(std::span<const std::byte> data);

/// The manifest in `folder`; nullopt when there is none (not installed, or installed by hand).
[[nodiscard]] std::optional<Manifest> ReadManifest(const std::filesystem::path& folder);

/// Writes `files` into `folder` and records them in the manifest under `version`. Each file is written next to its
/// target and then moved over it, so a failure never leaves a half-written file. Files a previous version installed
/// and this one does not are removed (unless the player changed a Keep file). Throws SetupError.
void InstallFiles(const std::filesystem::path& folder, std::span<const PayloadFile> files, std::string_view version);

struct RemoveResult {
    std::vector<std::wstring> kept;  // Keep files the player changed, relative to the folder
};

/// Removes what the manifest lists, the files BWDecomp.dll writes and the data folder if it is then empty. Throws
/// SetupError when there is no manifest or a file cannot be deleted (the manifest then still lists what remains).
RemoveResult RemoveFiles(const std::filesystem::path& folder);

/// Total size of `files` in bytes.
[[nodiscard]] std::uint64_t PayloadSize(std::span<const PayloadFile> files) noexcept;

}  // namespace bw::setup
