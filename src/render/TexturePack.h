#pragma once

// Texture dumping and replacement for the Direct3D 9 renderer ([Textures] in BWDecomp.ini; see docs/TEXTURES.md).
//
// Every texture the game draws is identified by a hash of its contents (format, size and top-level pixels as the game
// wrote them). With Dump=1 each distinct texture is saved once as Textures\Dump\bw_<hash>_<width>x<height>_<format>.png.
// A file in Textures\Replace\ (or any folder below it) whose name starts with bw_<hash> replaces that texture on the
// GPU: any size, 8 bits per channel with alpha, in any format Windows can decode (PNG, DDS, BMP, JPEG, TIFF). The game
// keeps working with its own copy, so it reads, edits and saves exactly what it did before.
//
// Neutral (no d3d9.h): Gpu.cpp asks for hashes and images and creates the textures.

#include "render/Format.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bw::render::textures {

struct Settings {
    bool dump = false;
    bool replace = true;
    std::filesystem::path folder;  ///< contains Dump\ and Replace\ (the game folder's Textures\)
};

/// Applies the settings and indexes the replacement files. Call once, before the game creates textures.
void Initialise(const Settings& settings) noexcept;

/// True when textures need hashing at all: dumping is on, or at least one replacement file exists.
[[nodiscard]] bool Active() noexcept;

/// The texture's identity: FNV-1a over format, size and the `width` x `height` image (rows `pitch` bytes apart,
/// padding excluded; rows of 4x4 blocks for compressed formats).
[[nodiscard]] std::uint64_t Hash(Format format, std::uint32_t width, std::uint32_t height, const std::uint8_t* bits,
                                 std::int32_t pitch) noexcept;

/// `bw_<16 hex digits>`, the start of every dump and replacement file name.
[[nodiscard]] std::wstring HashName(std::uint64_t hash);

/// Parses the hash from a file name starting with HashName; false otherwise.
[[nodiscard]] bool ParseHashName(std::wstring_view fileName, std::uint64_t& hash) noexcept;

/// A decoded image: `width` x `height` A8R8G8B8 pixels, row by row.
struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint32_t> argb;
};

/// True when Dump is on and the texture with `hash` has not been saved yet.
[[nodiscard]] bool WantsDump(std::uint64_t hash) noexcept;
/// Saves `image` (the texture's top level, in its colours as A8R8G8B8) as the dump of `hash`.
void Dump(std::uint64_t hash, Format format, const Image& image) noexcept;

/// True when a replacement file exists for `hash`.
[[nodiscard]] bool HasReplacement(std::uint64_t hash) noexcept;
/// Decodes the replacement for `hash`; false when there is none or it cannot be read (logged).
[[nodiscard]] bool LoadReplacement(std::uint64_t hash, Image& image) noexcept;

/// Halves an image with a 2x2 box filter (a 1-pixel side stays 1): the next mipmap level.
[[nodiscard]] Image HalveImage(const Image& image);

}  // namespace bw::render::textures
