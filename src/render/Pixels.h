#pragma once

// CPU pixel conversion between the renderer's formats (neutral; used by surface blits and uploads).

#include "render/Format.h"

#include <cstdint>

namespace bw::render {

/// Reads one pixel of `format` at `pixel` as A8R8G8B8 (alpha 255 for formats without alpha).
[[nodiscard]] std::uint32_t ReadArgb(Format format, const std::uint8_t* pixel) noexcept;
/// Writes A8R8G8B8 `argb` as one pixel of `format` (rounded to the format's precision).
void WriteArgb(Format format, std::uint32_t argb, std::uint8_t* pixel) noexcept;

/// Converts `width` pixels of `from` into A8R8G8B8.
void RowToArgb(Format from, const void* source, std::uint32_t* destination, std::uint32_t width) noexcept;

/// Decodes one 4x4 block of a DXT1-DXT5 texture into 16 A8R8G8B8 pixels (row by row). DXT2/DXT4 (premultiplied
/// alpha) decode like DXT3/DXT5.
void DecodeBlock(Format format, const std::uint8_t* block, std::uint32_t* pixels) noexcept;

/// Encodes 16 A8R8G8B8 pixels (a 4x4 block, row by row) as one DXT1/DXT3/DXT5 block: endpoints from the colour
/// range, each pixel's nearest of the four interpolated colours. Fast and simple, not a high-quality compressor.
void EncodeBlock(Format format, const std::uint32_t* pixels, std::uint8_t* block) noexcept;

/// Converts a `width` x `height` image of `from` (rows `pitch` bytes apart; rows of 4x4 blocks for compressed formats)
/// into A8R8G8B8, `width` pixels per row. `from` must be convertible (CanConvert).
void ImageToArgb(Format from, const std::uint8_t* bits, std::int32_t pitch, std::uint32_t width, std::uint32_t height,
                 std::uint32_t* destination) noexcept;

/// True when pixels of `format` carry alpha (DXT1 counts: its transparent texels).
[[nodiscard]] bool HasAlpha(Format format) noexcept;

/// True when CPU conversion between the two formats is supported (colour formats, including block-compressed ones).
[[nodiscard]] bool CanConvert(Format from, Format to) noexcept;

}  // namespace bw::render
