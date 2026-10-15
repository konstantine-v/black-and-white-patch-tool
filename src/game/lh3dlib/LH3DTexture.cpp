#include "game/lh3dlib/LH3DTexture.h"

#include "core/Call.h"
#include "core/Hook.h"
#include "core/Text.h"
#include "game/lh3dlib/LH3DRender.h"
#include "render/Renderer.h"
#include "render/TexturePack.h"

#include <cstring>
#include <format>
#include <memory>


namespace lh3dlib {

namespace {

// Not decompiled yet.
inline constexpr bw::CdeclFunction<0x00776a70, void(const char*, ...)> Trace{};
/// True when `path` is 256x256x3 bytes (a 24-bit texture file).
inline constexpr bw::CdeclFunction<0x0078cce0, std::int32_t(const char* path)> IsTextureFile{};
inline constexpr bw::CdeclFunction<0x0079a330, void*(std::size_t size)> Allocate{};
inline constexpr bw::CdeclFunction<0x0079a3b0, void(void* memory)> Free{};
/// Reads up to `capacity` bytes of `path` (or its packed `.zzz` form); 0 on success.
inline constexpr bw::CdeclFunction<0x0072c360, std::int32_t(const char* path, void* buffer, std::uint32_t capacity,
                                                            std::uint32_t* read)>
    ReadFile{};
/// The size of `path`; 0 on success.
inline constexpr bw::CdeclFunction<0x0072bed0, std::int32_t(const char* path, std::uint32_t* size)> GetFileSize{};
/// Creates a CompressedTexture from DDS data (null data: an empty 256x256 DXT1, or DXT3 when `dxt3`).
inline constexpr bw::CdeclFunction<0x007cc4a0, CompressedTexture*(const void* dds, std::int32_t dxt3)>
    CreateCompressedTexture{};
/// 2 when the segment does not exist.
inline constexpr bw::ThiscallFunction<0x0072d0c0, std::int32_t(LHReleasedFile*, const char* name,
                                                               ReleasedSegment* segment, std::int32_t flags)>
    LoadReleasedSegment{};
inline constexpr bw::ThiscallFunction<0x0072ce60, void(LHReleasedFile*)> ReleaseSegment{};

constexpr std::uint32_t kFileBytes = 256 * 256 * 3;
constexpr std::uint32_t kAlphaBytes = 256 * 256;
constexpr std::int32_t kAlwaysFullSize = 3;

void Store(const LockedImage16& image, std::int32_t x, std::int32_t y, std::uint16_t pixel) noexcept
{
    image.bits[image.pitch * y / 2 + x] = pixel;
}

}  // namespace

void __cdecl LoadRawTexture(LockedImage16* image, const char* path, std::int32_t stagingFormat,
                            std::int32_t alpha) noexcept
{
    if (IsTextureFile(path) == 0 || image->bits == nullptr) {
        return;
    }
    if ((g_textureFileBufferState & 1) == 0) {
        g_textureFileBufferState |= 1;
        auto* buffer = static_cast<std::uint8_t*>(Allocate(kFileBytes));
        if (buffer != nullptr) {
            std::memset(buffer, 0, kFileBytes);
        }
        g_textureFileBuffer = buffer;
    }
    std::uint32_t read = 0;
    // A failed read is not reported (the original formats a message it never shows) and converts what the buffer
    // holds.
    (void)ReadFile(path, g_textureFileBuffer, kFileBytes, &read);

    // With little video memory: every other pixel and row of the 256x256 file.
    const bool half = g_lowVideoMemory != 0 && stagingFormat != kAlwaysFullSize;
    std::size_t step = half ? 6 : 3;
    std::size_t rowSkip = half ? 0x300 : 0;

    // The renderer keeps the full colours (render/TexturePack.h).
    std::shared_ptr<bw::render::textures::Image> source;
    if (bw::render::Active() && bw::render::Options().fullPrecisionTextures) {
        try {
            source = std::make_shared<bw::render::textures::Image>();
            source->width = static_cast<std::uint32_t>(image->width);
            source->height = static_cast<std::uint32_t>(image->height);
            source->argb.resize(std::size_t(image->width) * image->height);
        }
        catch (const std::bad_alloc&) {
            source.reset();
        }
    }
    auto keep = [&](std::int32_t x, std::int32_t y, const std::uint8_t* rgb) {
        if (source) {
            source->argb[std::size_t(y) * source->width + x] =
                0xff000000u | std::uint32_t{rgb[0]} << 16 | std::uint32_t{rgb[1]} << 8 | rgb[2];
        }
    };

    const std::uint8_t* const file = g_textureFileBuffer;
    std::size_t offset = 0;
    bw::render::Format format = bw::render::Format::A4R4G4B4;
    if (alpha != 0) {
        for (std::int32_t y = 0; y < image->height; ++y) {
            for (std::int32_t x = 0; x < image->width; ++x) {
                const std::uint8_t* rgb = file + offset;
                Store(*image, x, y,
                      static_cast<std::uint16_t>((rgb[2] >> 4) + (rgb[1] & 0xf0) + ((rgb[0] & 0xf0) << 4)));
                keep(x, y, rgb);
                offset += step;
            }
            offset += rowSkip;
        }

        // The alpha file: the same name with "a" before ".raw" (built in a fixed buffer, as the original did).
        char alphaPath[0x800];
        bw::text::CopyInto(alphaPath, sizeof alphaPath, path);
        const std::size_t length = strnlen(alphaPath, sizeof alphaPath);
        alphaPath[length >= 4 ? length - 4 : 0] = '\0';
        bw::text::AppendInto(alphaPath, sizeof alphaPath, "a.raw");
        if (ReadFile(alphaPath, g_textureFileBuffer, kAlphaBytes, &read) != 0) {
            Trace("Can't load %s\n", alphaPath);
        }
        step = half ? 2 : 1;
        rowSkip = half ? 0x100 : 0;
        offset = 0;
        for (std::int32_t y = 0; y < image->height; ++y) {
            for (std::int32_t x = 0; x < image->width; ++x) {
                const std::uint8_t a = file[offset];
                image->bits[image->pitch * y / 2 + x] |= static_cast<std::uint16_t>((a & 0xf0) << 8);
                if (source) {
                    std::uint32_t& texel = source->argb[std::size_t(y) * source->width + x];
                    texel = (texel & 0x00ffffffu) | std::uint32_t{a} << 24;
                }
                offset += step;
            }
            offset += rowSkip;
        }
    }
    else if (g_opaqueTextures565 != 0) {
        format = bw::render::Format::R5G6B5;
        for (std::int32_t y = 0; y < image->height; ++y) {
            for (std::int32_t x = 0; x < image->width; ++x) {
                const std::uint8_t* rgb = file + offset;
                Store(*image, x, y, static_cast<std::uint16_t>(((rgb[0] & 0xf8) << 8) | ((rgb[1] & 0xf8) << 3) | (rgb[2] >> 3)));
                keep(x, y, rgb);
                offset += step;
            }
            offset += rowSkip;
        }
    }
    else {
        format = bw::render::Format::X1R5G5B5;
        for (std::int32_t y = 0; y < image->height; ++y) {
            for (std::int32_t x = 0; x < image->width; ++x) {
                const std::uint8_t* rgb = file + offset;
                Store(*image, x, y, static_cast<std::uint16_t>(((rgb[0] & 0xf8) << 7) | ((rgb[1] & 0xf8) << 2) | (rgb[2] >> 3)));
                keep(x, y, rgb);
                offset += step;
            }
            offset += rowSkip;
        }
    }

    if (bw::render::Active()) {
        try {
            const std::uint64_t hash = bw::render::textures::Hash(
                format, static_cast<std::uint32_t>(image->width), static_cast<std::uint32_t>(image->height),
                reinterpret_cast<const std::uint8_t*>(image->bits), image->pitch);
            bw::render::textures::RegisterSource(hash, {bw::render::textures::NormaliseName(path), std::move(source)});
        }
        catch (const std::bad_alloc&) {
        }
    }
}

void LH3DTexture::LoadSegment() noexcept
{
    if ((flags & kTextureKindMask) != static_cast<std::uint32_t>(TextureKind::Segment) || compressed != nullptr) {
        return;
    }
    char name[0x20]{};
    ReleasedSegment segment{};
    bw::text::FormatInto(name, sizeof name, "{}{:x}", g_lowVideoMemory != 0 ? "LOW" : "", static_cast<std::uint32_t>(id));
    if (LoadReleasedSegment(&g_textureSegments, name, &segment, 1) == 2) {
        return;  // the original formats "Cannot load segment %s" into a buffer it never shows
    }
    compressed = CreateCompressedTexture(segment.data + 0xc, 0);
    ReleaseSegment(&g_textureSegments);
    if (compressed != nullptr && compressed->surface != nullptr) {
        try {
            bw::render::NameTexture(compressed->surface, std::format("segments\\{:x}", static_cast<std::uint32_t>(id)));
        }
        catch (const std::exception&) {
        }
    }
}

CompressedTexture* __cdecl LoadCompressedTexture(const char* path) noexcept
{
    std::uint32_t size = 0;
    if (GetFileSize(path, &size) != 0) {
        return nullptr;  // the original formats "Can't load %s" into a buffer it never shows
    }
    void* data = Allocate(size);
    (void)ReadFile(path, data, size, nullptr);
    CompressedTexture* texture = CreateCompressedTexture(data, 0);
    Free(data);
    if (texture != nullptr && texture->surface != nullptr) {
        bw::render::NameTexture(texture->surface, path);
    }
    return texture;
}

BW_HOOK_FUNCTION(0x0078cde0, LoadRawTexture);
BW_HOOK_METHOD(0x0078ded0, LH3DTexture, LoadSegment);
BW_HOOK_FUNCTION(0x007cc750, LoadCompressedTexture);

}  // namespace lh3dlib
