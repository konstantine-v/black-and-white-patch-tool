#include "render/Format.h"

namespace bw::render {

std::uint32_t BytesPerPixel(Format format) noexcept
{
    switch (format) {
    case Format::L8:
    case Format::A8:
        return 1;
    case Format::R5G6B5:
    case Format::X1R5G5B5:
    case Format::A1R5G5B5:
    case Format::A4R4G4B4:
    case Format::A8L8:
    case Format::V8U8:
    case Format::D16:
        return 2;
    case Format::R8G8B8:
        return 3;
    case Format::X8R8G8B8:
    case Format::A8R8G8B8:
    case Format::X8L8V8U8:
    case Format::D24X8:
    case Format::D24S8:
    case Format::D32:
        return 4;
    default:
        return 0;
    }
}

bool IsBlockCompressed(Format format) noexcept
{
    return format >= Format::Dxt1 && format <= Format::Dxt5;
}

bool IsDepth(Format format) noexcept
{
    return format >= Format::D16 && format <= Format::D32;
}

std::uint32_t RowBytes(Format format, std::uint32_t width) noexcept
{
    if (IsBlockCompressed(format)) {
        const std::uint32_t blockBytes = format == Format::Dxt1 ? 8 : 16;
        return ((width + 3) / 4) * blockBytes;
    }
    return width * BytesPerPixel(format);
}

std::uint32_t StorageRows(Format format, std::uint32_t height) noexcept
{
    return IsBlockCompressed(format) ? (height + 3) / 4 : height;
}

std::string_view Name(Format format) noexcept
{
    constexpr std::string_view kNames[] = {"Unknown",  "R5G6B5", "X1R5G5B5", "A1R5G5B5", "A4R4G4B4", "R8G8B8",
                                           "X8R8G8B8", "A8R8G8B8", "L8",     "A8",       "A8L8",     "V8U8",
                                           "X8L8V8U8", "DXT1",   "DXT2",     "DXT3",     "DXT4",     "DXT5",
                                           "D16",      "D24X8",  "D24S8",    "D32"};
    const auto index = static_cast<std::size_t>(format);
    return index < std::size(kNames) ? kNames[index] : "?";
}

}  // namespace bw::render
