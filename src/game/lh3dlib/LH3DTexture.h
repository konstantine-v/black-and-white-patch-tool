#pragma once

// Lionhead 3D library textures (LH3DTexture, with LH3DVRAMTex for the pool of 256x256 GPU pages and LH3DSRAMTex for
// system-memory copies). Each LH3DTexture is one 256x256 image (128x128 with little video memory) from a source kept
// in its type; when it is drawn, it is copied into a free page of the pool, loading it again if it was evicted.
// Partially decompiled: the loaders that read texture files, so that the renderer's texture packs know where each
// texture came from (render/TexturePack.h).

#include "core/Memory.h"
#include "core/Windows.h"

#include <ddraw.h>

#include <cstddef>
#include <cstdint>

namespace lh3dlib {

/// LH3DTexture's type (low 6 bits of LH3DTexture::flags).
enum class TextureKind : std::uint32_t {
    File = 0x01,            ///< Data\Textures\*.raw: 24-bit RGB, 256x256, with an optional 8-bit <name>a.raw alpha file
    Segment = 0x02,         ///< a DDS segment of the texture resource file, by number
    Memory = 0x04,          ///< 16-bit pixels the game built in memory
    Generated = 0x08,       ///< drawn by a callback when needed
    CompressedFile = 0x10,  ///< a DDS file
    Compressed = 0x20,      ///< a DDS surface kept in memory
};
inline constexpr std::uint32_t kTextureKindMask = 0x3f;
inline constexpr std::uint32_t kTextureAlpha = 0x40;

/// A locked 16-bit image as the loaders receive it.
struct LockedImage16 {
    std::uint16_t* bits;
    std::int32_t pitch;  ///< bytes
    std::int32_t width;
    std::int32_t height;
};
static_assert(sizeof(LockedImage16) == 0x10);

/// A DirectDraw surface made from DDS data (0x007cc4a0); the game blits it into a page.
struct CompressedTexture {
    std::uint32_t size;
    IDirectDrawSurface7* surface;
};
static_assert(sizeof(CompressedTexture) == 8);

/// A segment loaded from an LHReleasedFile (the game's resource files).
struct ReleasedSegment {
    char name[0x24];
    std::uint32_t unknown24;
    const std::uint8_t* data;  ///< for textures: a 12-byte header, then a DDS file
};
static_assert(sizeof(ReleasedSegment) == 0x2c);

/// An LHReleasedFile; not decompiled.
struct LHReleasedFile;

class LH3DTexture {
public:
    /// Segment textures: loads segment `id` (hexadecimal name, "LOW" first with little video memory) from the texture
    /// resource file into `compressed`, once.
    void LoadSegment() noexcept;  // 0x0078ded0

    std::byte unknown00[0x10];
    std::uint32_t flags;           ///< 0x10: TextureKind, kTextureAlpha
    std::int32_t stagingFormat;    ///< 0x14: which staging surface it is loaded through (3: always full size)
    std::int32_t id;               ///< 0x18: segment number, or the callback's argument
    char path[0x100];              ///< 0x1c: file textures' file name
    std::int32_t number;           ///< 0x11c
    CompressedTexture* compressed; ///< 0x120: segment and compressed textures
};
static_assert(offsetof(LH3DTexture, flags) == 0x10);
static_assert(offsetof(LH3DTexture, path) == 0x1c);
static_assert(offsetof(LH3DTexture, compressed) == 0x120);

/// Loads a 24-bit texture file (`path`, 256x256) into `image` as 16-bit pixels: X1R5G5B5 or R5G6B5, or A4R4G4B4
/// with the alpha of <name>a.raw when `alpha` is set. With little video memory every other pixel and row is used
/// (a 128x128 image), except for staging format 3. Nothing happens when the file is not 256x256x3 bytes.
void __cdecl LoadRawTexture(LockedImage16* image, const char* path, std::int32_t stagingFormat,
                            std::int32_t alpha) noexcept;  // 0x0078cde0

/// Loads a DDS file into a CompressedTexture; null when the file does not exist.
[[nodiscard]] CompressedTexture* __cdecl LoadCompressedTexture(const char* path) noexcept;  // 0x007cc750

/// Shared buffer for texture files (allocated on first use, 256x256x3 bytes) and its initialisation guard.
BW_GLOBAL(std::uint8_t*, g_textureFileBuffer, 0x00df8178);
BW_GLOBAL(std::uint8_t, g_textureFileBufferState, 0x00df817c);
/// 1 when opaque textures are R5G6B5, 0 when X1R5G5B5.
BW_GLOBAL(std::int32_t, g_opaqueTextures565, 0x00df8208);
/// The texture resource file that segments are loaded from.
BW_GLOBAL(LHReleasedFile, g_textureSegments, 0x00df8180);

}  // namespace lh3dlib
