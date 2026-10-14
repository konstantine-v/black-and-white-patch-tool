#pragma once

// Pixel formats shared by the DirectDraw side (DDPIXELFORMAT) and the Direct3D 9 side (D3DFORMAT) of the renderer.
// Neutral: includes neither ddraw.h nor d3d9.h, whose type names collide.

#include <cstdint>
#include <string_view>

namespace bw::render {

enum class Format : std::uint8_t {
    Unknown,
    R5G6B5,
    X1R5G5B5,
    A1R5G5B5,
    A4R4G4B4,
    R8G8B8,
    X8R8G8B8,
    A8R8G8B8,
    L8,
    A8,
    A8L8,
    V8U8,
    X8L8V8U8,
    Dxt1,
    Dxt2,
    Dxt3,
    Dxt4,
    Dxt5,
    D16,
    D24X8,
    D24S8,
    D32,
};

/// Bytes per pixel, or 0 for block-compressed formats.
[[nodiscard]] std::uint32_t BytesPerPixel(Format format) noexcept;
[[nodiscard]] bool IsBlockCompressed(Format format) noexcept;
[[nodiscard]] bool IsDepth(Format format) noexcept;
/// Bytes in one row of `width` pixels (rows of 4x4 blocks for compressed formats).
[[nodiscard]] std::uint32_t RowBytes(Format format, std::uint32_t width) noexcept;
/// Rows of storage for `height` pixels (block rows for compressed formats).
[[nodiscard]] std::uint32_t StorageRows(Format format, std::uint32_t height) noexcept;
[[nodiscard]] std::string_view Name(Format format) noexcept;

}  // namespace bw::render
