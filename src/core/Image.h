#pragma once

#include "core/Memory.h"
#include "core/Windows.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

/// Identification of the executable the decompilation was built against.
namespace bw::image {

using Sha256 = std::array<std::byte, 32>;

struct Section {
    Address begin;
    Address end;
    [[nodiscard]] std::size_t Size() const noexcept { return end - begin; }
};

/// The only supported build: Black & White 1.00 (English, 2001-03-09 link time), the unprotected runblack.exe
/// shipped in the disc's crack\ folder. The retail runblack.exe is the same build wrapped in SafeDisc.
inline constexpr std::string_view kSupportedFileSha256 =
    "86a30b08ab2ea8a240403d37f8f0303cf65aa5cde8e7f673049ab01654ccf755";
inline constexpr std::string_view kSupportedTextSha256 =
    "50f9e8d7f251e145dda576b29f4efede6b8e734cd7b59dbb730d928100b0d8a8";
/// The SafeDisc-protected retail runblack.exe from the same disc (needs secdrv.sys, which Windows no longer loads).
inline constexpr std::string_view kRetailSafeDiscFileSha256 =
    "b1163b44f4e6c5e64d81d5fcce95dea8efdc587ad52910df4d4942e01dbfb3d1";

/// The NT headers of a module mapped by the loader, after checking the DOS and PE signatures; nullptr when `module`
/// is not a 32-bit PE image.
[[nodiscard]] const IMAGE_NT_HEADERS32* NtHeaders(HMODULE module) noexcept;

[[nodiscard]] std::optional<Section> FindSection(HMODULE module, std::string_view name) noexcept;

[[nodiscard]] std::optional<Sha256> ComputeSha256(std::span<const std::byte> data) noexcept;

/// Lower-case hexadecimal, as printed by sha256sum.
[[nodiscard]] std::array<char, 64> ToHex(const Sha256& digest) noexcept;

/// True when `module` is the supported runblack.exe: its .text section hashes to kSupportedTextSha256. Must be called
/// before any hook is written.
[[nodiscard]] bool IsSupportedGame(HMODULE module) noexcept;

}  // namespace bw::image
