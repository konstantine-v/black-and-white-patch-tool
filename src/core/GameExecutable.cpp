#include "core/GameExecutable.h"

#include "core/Image.h"
#include "core/Unicode.h"

#include <fstream>
#include <span>
#include <vector>

namespace bw {

std::optional<std::string> FileSha256(const std::filesystem::path& file, std::uintmax_t largest)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(file, error);
    if (error || size > largest) {
        return std::nullopt;
    }
    std::vector<char> bytes(static_cast<std::size_t>(size));
    std::ifstream stream(file, std::ios::binary);
    if (!stream || !stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        return std::nullopt;
    }
    const std::optional<image::Sha256> digest = image::ComputeSha256(std::as_bytes(std::span(bytes)));
    if (!digest) {
        return std::nullopt;
    }
    const auto hex = image::ToHex(*digest);
    return std::string(hex.data(), hex.size());
}

GameExecutableCheck CheckGameExecutable(const std::filesystem::path& file)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) {
        return {GameBuild::Missing, {}};
    }
    constexpr std::uintmax_t kLargestExecutable = 64u << 20;  // runblack.exe is 8 MB
    std::optional<std::string> hash = FileSha256(file, kLargestExecutable);
    if (!hash) {
        return {GameBuild::Unreadable, {}};
    }
    const GameBuild build = *hash == image::kSupportedFileSha256        ? GameBuild::Supported
                            : *hash == image::kRetailSafeDiscFileSha256 ? GameBuild::RetailSafeDisc
                                                                         : GameBuild::Unknown;
    return {build, std::move(*hash)};
}

std::wstring DescribeGameExecutable(const GameExecutableCheck& check, const std::filesystem::path& file)
{
    switch (check.build) {
    case GameBuild::Supported:
        return {};
    case GameBuild::Missing:
        return L"Cannot find " + file.wstring() + L".";
    case GameBuild::Unreadable:
        return L"Cannot read " + file.wstring() + L".";
    case GameBuild::RetailSafeDisc:
        return L"This runblack.exe is the SafeDisc-protected retail build. SafeDisc needs the secdrv.sys driver, "
               L"which current Windows versions refuse to load, so this file cannot start.\n\n"
               L"The patch targets the unprotected build of the same version that is on the Black & White "
               L"disc (crack\\runblack.exe). Copy that file over the installed runblack.exe and start again.";
    case GameBuild::Unknown:
        break;
    }
    return L"Unsupported runblack.exe (SHA-256 " + Widen(check.sha256) +
           L").\n\nThe patch only supports Black & White 1.00 (English) with SHA-256 " +
           Widen(image::kSupportedFileSha256) + L".";
}

}  // namespace bw
