#include "render/Pixels.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace bw::render {

namespace {

/// Expands an n-bit channel to 8 bits by replicating its high bits (31 -> 255, 16 -> 132).
constexpr std::uint32_t Expand(std::uint32_t value, int bits) noexcept
{
    const std::uint32_t shifted = value << (8 - bits);
    return shifted | (shifted >> bits);
}

constexpr std::uint32_t Pack(std::uint32_t a, std::uint32_t r, std::uint32_t g, std::uint32_t b) noexcept
{
    return (a << 24) | (r << 16) | (g << 8) | b;
}

std::uint16_t Read16(const std::uint8_t* pixel) noexcept
{
    std::uint16_t value;
    std::memcpy(&value, pixel, sizeof value);
    return value;
}

std::uint32_t Read32(const std::uint8_t* pixel) noexcept
{
    std::uint32_t value;
    std::memcpy(&value, pixel, sizeof value);
    return value;
}

void Write16(std::uint8_t* pixel, std::uint32_t value) noexcept
{
    const auto narrow = static_cast<std::uint16_t>(value);
    std::memcpy(pixel, &narrow, sizeof narrow);
}

void Write32(std::uint8_t* pixel, std::uint32_t value) noexcept
{
    std::memcpy(pixel, &value, sizeof value);
}

/// Rounds an 8-bit channel to `bits` bits.
constexpr std::uint32_t Reduce(std::uint32_t value, int bits) noexcept
{
    const std::uint32_t maximum = (1u << bits) - 1;
    return (value * maximum + 127) / 255;
}

}  // namespace

std::uint32_t ReadArgb(Format format, const std::uint8_t* pixel) noexcept
{
    switch (format) {
    case Format::R5G6B5: {
        const std::uint32_t v = Read16(pixel);
        return Pack(255, Expand(v >> 11, 5), Expand((v >> 5) & 63, 6), Expand(v & 31, 5));
    }
    case Format::X1R5G5B5:
    case Format::A1R5G5B5: {
        const std::uint32_t v = Read16(pixel);
        const std::uint32_t a = format == Format::X1R5G5B5 || (v & 0x8000) != 0 ? 255 : 0;
        return Pack(a, Expand((v >> 10) & 31, 5), Expand((v >> 5) & 31, 5), Expand(v & 31, 5));
    }
    case Format::A4R4G4B4: {
        const std::uint32_t v = Read16(pixel);
        return Pack(Expand(v >> 12, 4), Expand((v >> 8) & 15, 4), Expand((v >> 4) & 15, 4), Expand(v & 15, 4));
    }
    case Format::R8G8B8:
        return Pack(255, pixel[2], pixel[1], pixel[0]);
    case Format::X8R8G8B8:
        return Read32(pixel) | 0xff000000u;
    case Format::A8R8G8B8:
        return Read32(pixel);
    case Format::L8:
        return Pack(255, pixel[0], pixel[0], pixel[0]);
    case Format::A8:
        return Pack(pixel[0], 0, 0, 0);
    case Format::A8L8:
        return Pack(pixel[1], pixel[0], pixel[0], pixel[0]);
    default:
        return 0;
    }
}

void WriteArgb(Format format, std::uint32_t argb, std::uint8_t* pixel) noexcept
{
    const std::uint32_t a = argb >> 24;
    const std::uint32_t r = (argb >> 16) & 255;
    const std::uint32_t g = (argb >> 8) & 255;
    const std::uint32_t b = argb & 255;
    switch (format) {
    case Format::R5G6B5:
        Write16(pixel, (Reduce(r, 5) << 11) | (Reduce(g, 6) << 5) | Reduce(b, 5));
        break;
    case Format::X1R5G5B5:
    case Format::A1R5G5B5:
        Write16(pixel, ((a >= 128 ? 1u : 0u) << 15) | (Reduce(r, 5) << 10) | (Reduce(g, 5) << 5) | Reduce(b, 5));
        break;
    case Format::A4R4G4B4:
        Write16(pixel, (Reduce(a, 4) << 12) | (Reduce(r, 4) << 8) | (Reduce(g, 4) << 4) | Reduce(b, 4));
        break;
    case Format::R8G8B8:
        pixel[0] = static_cast<std::uint8_t>(b);
        pixel[1] = static_cast<std::uint8_t>(g);
        pixel[2] = static_cast<std::uint8_t>(r);
        break;
    case Format::X8R8G8B8:
    case Format::A8R8G8B8:
        Write32(pixel, argb);
        break;
    case Format::L8:
        pixel[0] = static_cast<std::uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
        break;
    case Format::A8:
        pixel[0] = static_cast<std::uint8_t>(a);
        break;
    case Format::A8L8:
        pixel[0] = static_cast<std::uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
        pixel[1] = static_cast<std::uint8_t>(a);
        break;
    default:
        break;
    }
}

void RowToArgb(Format from, const void* source, std::uint32_t* destination, std::uint32_t width) noexcept
{
    const auto* bytes = static_cast<const std::uint8_t*>(source);
    if (from == Format::A8R8G8B8) {
        std::memcpy(destination, bytes, std::size_t{width} * 4);
        return;
    }
    const std::uint32_t step = BytesPerPixel(from);
    for (std::uint32_t x = 0; x < width; ++x) {
        destination[x] = ReadArgb(from, bytes + std::size_t{x} * step);
    }
}

namespace {

std::uint32_t Rgb565ToArgb(std::uint32_t value) noexcept
{
    return Pack(255, Expand(value >> 11, 5), Expand((value >> 5) & 63, 6), Expand(value & 31, 5));
}

std::uint32_t Mix(std::uint32_t a, std::uint32_t b, std::uint32_t weightA, std::uint32_t weightB,
                  std::uint32_t total) noexcept
{
    std::uint32_t result = 0xff000000u;
    for (int shift = 0; shift < 24; shift += 8) {
        const std::uint32_t channel = (((a >> shift) & 255) * weightA + ((b >> shift) & 255) * weightB) / total;
        result |= channel << shift;
    }
    return result;
}

/// The colour part shared by all DXT formats: two RGB565 endpoints and 2-bit indices.
void DecodeColors(const std::uint8_t* block, std::uint32_t* pixels, bool allowTransparent) noexcept
{
    const std::uint32_t c0 = block[0] | (block[1] << 8);
    const std::uint32_t c1 = block[2] | (block[3] << 8);
    std::uint32_t palette[4] = {Rgb565ToArgb(c0), Rgb565ToArgb(c1), 0, 0};
    if (c0 > c1 || !allowTransparent) {
        palette[2] = Mix(palette[0], palette[1], 2, 1, 3);
        palette[3] = Mix(palette[0], palette[1], 1, 2, 3);
    }
    else {
        palette[2] = Mix(palette[0], palette[1], 1, 1, 2);
        palette[3] = 0;  // transparent black
    }
    const std::uint32_t indices = block[4] | (block[5] << 8) | (block[6] << 16) | (std::uint32_t{block[7]} << 24);
    for (int i = 0; i < 16; ++i) {
        pixels[i] = palette[(indices >> (2 * i)) & 3];
    }
}

}  // namespace

void DecodeBlock(Format format, const std::uint8_t* block, std::uint32_t* pixels) noexcept
{
    if (format == Format::Dxt1) {
        DecodeColors(block, pixels, true);
        return;
    }
    DecodeColors(block + 8, pixels, false);
    if (format == Format::Dxt2 || format == Format::Dxt3) {
        for (int i = 0; i < 16; ++i) {
            const std::uint32_t alpha = (block[i / 2] >> (4 * (i & 1))) & 15;
            pixels[i] = (pixels[i] & 0x00ffffffu) | (Expand(alpha, 4) << 24);
        }
        return;
    }
    // DXT4/DXT5: two 8-bit alpha endpoints and 3-bit indices.
    const std::uint32_t a0 = block[0];
    const std::uint32_t a1 = block[1];
    std::uint32_t alphas[8] = {a0, a1};
    if (a0 > a1) {
        for (std::uint32_t i = 1; i < 7; ++i) {
            alphas[i + 1] = ((7 - i) * a0 + i * a1) / 7;
        }
    }
    else {
        for (std::uint32_t i = 1; i < 5; ++i) {
            alphas[i + 1] = ((5 - i) * a0 + i * a1) / 5;
        }
        alphas[6] = 0;
        alphas[7] = 255;
    }
    std::uint64_t indices = 0;
    for (int i = 0; i < 6; ++i) {
        indices |= std::uint64_t{block[2 + i]} << (8 * i);
    }
    for (int i = 0; i < 16; ++i) {
        pixels[i] = (pixels[i] & 0x00ffffffu) | (alphas[(indices >> (3 * i)) & 7] << 24);
    }
}

namespace {

std::uint32_t ArgbTo565(std::uint32_t argb) noexcept
{
    return (Reduce((argb >> 16) & 255, 5) << 11) | (Reduce((argb >> 8) & 255, 6) << 5) | Reduce(argb & 255, 5);
}

int Distance(std::uint32_t a, std::uint32_t b) noexcept
{
    int sum = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        const int difference = int((a >> shift) & 255) - int((b >> shift) & 255);
        sum += difference * difference;
    }
    return sum;
}

void EncodeColors(const std::uint32_t* pixels, std::uint8_t* block, bool transparency) noexcept
{
    // Endpoints: the brightest and darkest pixels (by luminance) of the opaque ones.
    std::uint32_t low = 0xffffffff;
    std::uint32_t high = 0;
    std::uint32_t lowLuma = ~0u;
    std::uint32_t highLuma = 0;
    bool anyTransparent = false;
    for (int i = 0; i < 16; ++i) {
        if (transparency && (pixels[i] >> 24) < 128) {
            anyTransparent = true;
            continue;
        }
        const std::uint32_t luma = ((pixels[i] >> 16) & 255) * 77 + ((pixels[i] >> 8) & 255) * 150 + (pixels[i] & 255) * 29;
        if (luma <= lowLuma) {
            lowLuma = luma;
            low = pixels[i];
        }
        if (luma >= highLuma) {
            highLuma = luma;
            high = pixels[i];
        }
    }
    if (low == 0xffffffff) {
        low = high = 0;  // all transparent
    }
    std::uint32_t c0 = ArgbTo565(high);
    std::uint32_t c1 = ArgbTo565(low);
    const bool threeColour = transparency && anyTransparent;
    if (threeColour ? c0 > c1 : c0 < c1) {
        std::swap(c0, c1);  // c0 <= c1 selects the 3-colour + transparent mode
    }
    if (!threeColour && c0 == c1) {
        c1 = c0 > 0 ? c0 - 1 : c0;  // keep 4-colour mode (c0 > c1) for a flat block
        if (c0 == c1) {
            c0 = c0 + 1;
        }
    }
    block[0] = static_cast<std::uint8_t>(c0);
    block[1] = static_cast<std::uint8_t>(c0 >> 8);
    block[2] = static_cast<std::uint8_t>(c1);
    block[3] = static_cast<std::uint8_t>(c1 >> 8);
    std::uint32_t palette[4];
    std::uint8_t decoded[8] = {block[0], block[1], block[2], block[3], 0, 0, 0, 0};
    std::uint32_t reference[16];
    DecodeBlock(Format::Dxt1, decoded, reference);
    palette[0] = reference[0];  // indices 0 decode to entry 0
    // Recover the four palette entries by decoding blocks whose indices select each entry.
    for (std::uint32_t entry = 0; entry < 4; ++entry) {
        const std::uint8_t pattern = static_cast<std::uint8_t>(entry * 0x55);
        std::uint8_t probe[8] = {block[0], block[1], block[2], block[3], pattern, pattern, pattern, pattern};
        DecodeBlock(Format::Dxt1, probe, reference);
        palette[entry] = reference[0];
    }
    std::uint32_t indices = 0;
    for (int i = 0; i < 16; ++i) {
        std::uint32_t best = 0;
        if (threeColour && (pixels[i] >> 24) < 128) {
            best = 3;
        }
        else {
            int bestDistance = Distance(pixels[i], palette[0]);
            const std::uint32_t entries = threeColour ? 3 : 4;
            for (std::uint32_t entry = 1; entry < entries; ++entry) {
                if (const int distance = Distance(pixels[i], palette[entry]); distance < bestDistance) {
                    bestDistance = distance;
                    best = entry;
                }
            }
        }
        indices |= best << (2 * i);
    }
    for (int i = 0; i < 4; ++i) {
        block[4 + i] = static_cast<std::uint8_t>(indices >> (8 * i));
    }
}

}  // namespace

void EncodeBlock(Format format, const std::uint32_t* pixels, std::uint8_t* block) noexcept
{
    if (format == Format::Dxt1) {
        EncodeColors(pixels, block, true);
        return;
    }
    EncodeColors(pixels, block + 8, false);
    if (format == Format::Dxt2 || format == Format::Dxt3) {
        for (int i = 0; i < 8; ++i) {
            block[i] = static_cast<std::uint8_t>(Reduce(pixels[2 * i] >> 24, 4) | (Reduce(pixels[2 * i + 1] >> 24, 4) << 4));
        }
        return;
    }
    // DXT4/DXT5: alpha endpoints = alpha range, 3-bit indices to the nearest of the 8 interpolated values.
    std::uint32_t a0 = 0;
    std::uint32_t a1 = 255;
    for (int i = 0; i < 16; ++i) {
        a0 = std::max(a0, pixels[i] >> 24);
        a1 = std::min(a1, pixels[i] >> 24);
    }
    if (a0 == a1) {
        a1 = a0 > 0 ? a0 - 1 : 0;
        if (a0 == a1) {
            a0 = 1;
        }
    }
    std::uint32_t alphas[8] = {a0, a1};
    for (std::uint32_t i = 1; i < 7; ++i) {
        alphas[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    }
    std::uint64_t indices = 0;
    for (int i = 0; i < 16; ++i) {
        const int alpha = int(pixels[i] >> 24);
        std::uint64_t best = 0;
        int bestDistance = 1 << 30;
        for (std::uint32_t entry = 0; entry < 8; ++entry) {
            const int distance = std::abs(alpha - int(alphas[entry]));
            if (distance < bestDistance) {
                bestDistance = distance;
                best = entry;
            }
        }
        indices |= best << (3 * i);
    }
    block[0] = static_cast<std::uint8_t>(a0);
    block[1] = static_cast<std::uint8_t>(a1);
    for (int i = 0; i < 6; ++i) {
        block[2 + i] = static_cast<std::uint8_t>(indices >> (8 * i));
    }
}

void ImageToArgb(Format from, const std::uint8_t* bits, std::int32_t pitch, std::uint32_t width, std::uint32_t height,
                 std::uint32_t* destination) noexcept
{
    if (!IsBlockCompressed(from)) {
        for (std::uint32_t y = 0; y < height; ++y) {
            RowToArgb(from, bits + std::size_t{y} * pitch, destination + std::size_t{y} * width, width);
        }
        return;
    }
    const std::size_t blockBytes = from == Format::Dxt1 ? 8 : 16;
    std::uint32_t block[16];
    for (std::uint32_t by = 0; by < height; by += 4) {
        for (std::uint32_t bx = 0; bx < width; bx += 4) {
            DecodeBlock(from, bits + std::size_t{by / 4} * pitch + std::size_t{bx / 4} * blockBytes, block);
            for (std::uint32_t py = 0; py < 4 && by + py < height; ++py) {
                for (std::uint32_t px = 0; px < 4 && bx + px < width; ++px) {
                    destination[std::size_t{by + py} * width + bx + px] = block[py * 4 + px];
                }
            }
        }
    }
}

bool HasAlpha(Format format) noexcept
{
    switch (format) {
    case Format::A1R5G5B5:
    case Format::A4R4G4B4:
    case Format::A8R8G8B8:
    case Format::A8:
    case Format::A8L8:
    case Format::Dxt1:
    case Format::Dxt2:
    case Format::Dxt3:
    case Format::Dxt4:
    case Format::Dxt5:
        return true;
    default:
        return false;
    }
}

bool CanConvert(Format from, Format to) noexcept
{
    auto convertible = [](Format format) {
        return BytesPerPixel(format) != 0 && !IsDepth(format) && format != Format::V8U8 &&
               format != Format::X8L8V8U8;
    };
    return (convertible(from) || IsBlockCompressed(from)) && (convertible(to) || IsBlockCompressed(to));
}

}  // namespace bw::render
