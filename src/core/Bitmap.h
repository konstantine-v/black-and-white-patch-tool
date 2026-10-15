#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

/// Images in memory and on disk (PNG, DDS, BMP, JPEG, TIFF through Windows Imaging Component). Used by screenshots
/// and texture packs.
namespace bw {

/// `width` x `height` A8R8G8B8 pixels (0xAARRGGBB, B G R A in memory), row by row without padding.
struct Bitmap {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint32_t> argb;
};

/// The largest side ReadBitmap accepts: the largest texture current GPUs take, and small enough that the pixel count
/// cannot overflow a 32-bit size.
inline constexpr std::uint32_t kMaxBitmapSide = 16384;

/// Writes `bitmap` (with its alpha channel) as a PNG file. False when it cannot be written.
[[nodiscard]] bool WritePng(const std::filesystem::path& path, const Bitmap& bitmap) noexcept;

/// Decodes the first frame of an image file into A8R8G8B8. False when the file cannot be read, is not an image, or
/// is larger than kMaxBitmapSide on either side.
[[nodiscard]] bool ReadBitmap(const std::filesystem::path& path, Bitmap& bitmap) noexcept;

}  // namespace bw
