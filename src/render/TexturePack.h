#pragma once

// Texture dumping, replacement and full-colour sources for the Direct3D 9 renderer ([Textures] in BWDecomp.ini; see
// docs/TEXTURES.md).
//
// The game keeps every texture as one 256x256 image (LH3DTexture) and copies it into a pool of GPU pages when it is
// drawn. A page is identified by a hash of its contents (format, size and pixels as the game wrote them). The game's
// texture loaders (game/lh3dlib/LH3DTexture.cpp) register the source of what they load under that hash: a name such
// as `data\textures\sky` or `segments\1a3`, and for 24-bit files the image at full colour. A texture without a
// registered source is known by its hash, `bw_<16 hex digits>`.
//
// With Dump=1 each distinct texture is saved once to Textures\Dump\ as <name>.png (or bw_<hash>_<w>x<h>_<format>.png).
// An image in Textures\Replace\ at the same relative path (any extension Windows can decode), or one whose file name
// starts with bw_<hash>, is drawn instead: any size, 8 bits per channel with alpha. The game keeps working with its
// own copy.
//
// Neutral (no d3d9.h): Gpu.cpp asks for hashes, sources and images and creates the textures.

#include "core/Bitmap.h"
#include "render/Format.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bw::render::textures {

struct Settings {
    bool dump = false;
    bool replace = true;
    std::filesystem::path folder;  ///< contains Dump\ and Replace\ (the game folder's Textures\)
};

/// Applies the settings and indexes the replacement files. Call once, before the game creates textures.
void Initialise(const Settings& settings) noexcept;

/// True when textures need hashing at all: dumping is on, replacement files exist, or a source has full colour.
[[nodiscard]] bool Active() noexcept;

/// The texture's identity: FNV-1a over format, size and the `width` x `height` image (rows `pitch` bytes apart,
/// padding excluded; rows of 4x4 blocks for compressed formats).
[[nodiscard]] std::uint64_t Hash(Format format, std::uint32_t width, std::uint32_t height, const std::uint8_t* bits,
                                 std::int32_t pitch) noexcept;

/// `bw_<16 hex digits>`, the start of every hash-named dump and replacement file.
[[nodiscard]] std::wstring HashName(std::uint64_t hash);

/// Parses the hash from a file name starting with HashName (any case); false otherwise.
[[nodiscard]] bool ParseHashName(std::wstring_view fileName, std::uint64_t& hash) noexcept;

/// A source name as the renderer keys it: lower case, backslashes, without a leading ".\" and without the extension
/// (".\Data\Textures\Sky.raw" becomes "data\textures\sky").
[[nodiscard]] std::string NormaliseName(std::string_view path);

/// A decoded image (core/Bitmap.h).
using Image = Bitmap;

/// Where a texture came from.
struct Source {
    std::string name;                    ///< NormaliseName form
    std::shared_ptr<const Image> image;  ///< the source at full colour, when it has more than the texture's format
};

/// Records that a texture whose contents hash to `hash` was loaded from `source` (later registrations win).
void RegisterSource(std::uint64_t hash, Source source) noexcept;
/// The source registered for `hash`; false when none.
[[nodiscard]] bool FindSource(std::uint64_t hash, Source& source) noexcept;

/// True when Dump is on and the texture with `hash` has not been saved yet.
[[nodiscard]] bool WantsDump(std::uint64_t hash) noexcept;
/// Saves `image` (the texture's top level as A8R8G8B8) as the dump of `hash`: Dump\<name>.png when `name` is set.
void Dump(std::uint64_t hash, std::string_view name, Format format, const Image& image) noexcept;

/// The replacement file for a texture: by source name first, then by hash. Empty when there is none.
[[nodiscard]] std::filesystem::path FindReplacement(std::uint64_t hash, std::string_view name) noexcept;
/// Decodes an image file through Windows Imaging Component; false (logged) when it cannot be read.
[[nodiscard]] bool LoadImage(const std::filesystem::path& path, Image& image) noexcept;

/// Halves an image with a 2x2 box filter (a 1-pixel side stays 1): the next mipmap level.
[[nodiscard]] Image HalveImage(const Image& image);

}  // namespace bw::render::textures
