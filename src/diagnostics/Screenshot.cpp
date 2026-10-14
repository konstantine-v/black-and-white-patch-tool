#include "diagnostics/Screenshot.h"

#include "core/Log.h"

#include <ddraw.h>

#include <array>
#include <atomic>
#include <bit>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>

namespace bw::diagnostics {

namespace {

HANDLE g_requestEvent = nullptr;
int g_screenshotKey = 0;
std::atomic<bool> g_keyWasDown = false;

constexpr std::array<std::uint32_t, 256> MakeCrcTable() noexcept
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < table.size(); ++n) {
        std::uint32_t c = n;
        for (int bit = 0; bit < 8; ++bit) {
            c = (c & 1) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        }
        table[n] = c;
    }
    return table;
}

constexpr std::array<std::uint32_t, 256> kCrcTable = MakeCrcTable();

void AppendBigEndian(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void AppendChunk(std::vector<std::uint8_t>& out, const char (&type)[5], std::span<const std::uint8_t> data)
{
    AppendBigEndian(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t typeStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    AppendBigEndian(out, Crc32(std::span(out).subspan(typeStart)));
}

bool KeyPressed() noexcept
{
    if (g_screenshotKey == 0) {
        return false;
    }
    const bool down = (GetAsyncKeyState(g_screenshotKey) & 0x8000) != 0;
    const bool wasDown = g_keyWasDown.exchange(down);
    if (!down || wasDown) {
        return false;
    }
    DWORD foregroundProcess = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
    return foregroundProcess == GetCurrentProcessId();
}

std::filesystem::path ScreenshotPath()
{
    std::wstring exe(MAX_PATH, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const std::wstring name = std::format(L"BW_{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}.png", now.wYear, now.wMonth,
                                          now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return std::filesystem::path(exe).parent_path() / L"Screenshots" / name;
}

}  // namespace

void InstallScreenshots(const Config& config) noexcept
{
    g_screenshotKey = static_cast<int>(config.screenshotKey);
    const std::wstring name = std::format(L"Local\\BWDecomp.Screenshot.{}", GetCurrentProcessId());
    g_requestEvent = CreateEventW(nullptr, FALSE, FALSE, name.c_str());
    if (g_requestEvent == nullptr) {
        log::Warning("Screenshots: cannot create the request event ({})", GetLastError());
    }
}

bool ScreenshotRequested() noexcept
{
    const bool signalled = g_requestEvent != nullptr && WaitForSingleObject(g_requestEvent, 0) == WAIT_OBJECT_0;
    return KeyPressed() || signalled;
}

void SaveScreenshot(IDirectDrawSurface* surface) noexcept
{
    if (surface == nullptr) {
        return;
    }
    IDirectDrawSurface7* surface7 = nullptr;
    if (FAILED(surface->QueryInterface(IID_IDirectDrawSurface7, reinterpret_cast<void**>(&surface7)))) {
        log::Warning("Screenshot: the back buffer has no IDirectDrawSurface7 interface");
        return;
    }
    try {
        DDSURFACEDESC2 description{};
        description.dwSize = sizeof description;
        HRESULT result = surface7->GetSurfaceDesc(&description);
        const PixelFormat format{description.ddpfPixelFormat.dwRGBBitCount, description.ddpfPixelFormat.dwRBitMask,
                                 description.ddpfPixelFormat.dwGBitMask, description.ddpfPixelFormat.dwBBitMask};
        if (SUCCEEDED(result) && format.bitsPerPixel != 16 && format.bitsPerPixel != 32) {
            log::Warning("Screenshot: {}-bit surfaces are not supported", format.bitsPerPixel);
            surface7->Release();
            return;
        }
        const std::uint32_t width = description.dwWidth;
        const std::uint32_t height = description.dwHeight;
        std::vector<std::uint8_t> rgb(std::size_t{width} * height * 3);

        if (SUCCEEDED(result)) {
            result = surface7->Lock(nullptr, &description, DDLOCK_WAIT | DDLOCK_READONLY | DDLOCK_SURFACEMEMORYPTR,
                                    nullptr);
        }
        if (FAILED(result)) {
            log::Warning("Screenshot: cannot read the back buffer (0x{:08x})", static_cast<std::uint32_t>(result));
            surface7->Release();
            return;
        }
        const auto* rows = static_cast<const std::uint8_t*>(description.lpSurface);
        for (std::uint32_t y = 0; y < height; ++y) {
            const std::uint8_t* row = rows + std::size_t{y} * description.lPitch;
            std::uint8_t* out = rgb.data() + std::size_t{y} * width * 3;
            for (std::uint32_t x = 0; x < width; ++x) {
                std::uint32_t pixel = 0;
                if (format.bitsPerPixel == 32) {
                    pixel = reinterpret_cast<const std::uint32_t*>(row)[x];
                }
                else {
                    pixel = reinterpret_cast<const std::uint16_t*>(row)[x];
                }
                *out++ = ExtractChannel(pixel, format.redMask);
                *out++ = ExtractChannel(pixel, format.greenMask);
                *out++ = ExtractChannel(pixel, format.blueMask);
            }
        }
        surface7->Unlock(nullptr);
        surface7->Release();
        surface7 = nullptr;

        const std::filesystem::path path = ScreenshotPath();
        std::filesystem::create_directories(path.parent_path());
        const std::vector<std::uint8_t> png = EncodePng(width, height, rgb);
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(png.data()),
                                                    static_cast<std::streamsize>(png.size()));
        log::Info("Screenshot: {}", path.string());
    }
    catch (const std::exception& error) {
        log::Warning("Screenshot failed: {}", error.what());
        if (surface7 != nullptr) {
            surface7->Release();
        }
    }
}

std::uint8_t ExtractChannel(std::uint32_t pixel, std::uint32_t mask) noexcept
{
    if (mask == 0) {
        return 0;
    }
    const int shift = std::countr_zero(mask);
    const int bits = std::popcount(mask);
    const std::uint32_t value = (pixel & mask) >> shift;
    const std::uint32_t maximum = (std::uint32_t{1} << bits) - 1;
    return static_cast<std::uint8_t>((value * 255 + maximum / 2) / maximum);
}

std::vector<std::uint8_t> EncodePng(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgb)
{
    const std::size_t rowBytes = std::size_t{width} * 3;
    // Every row starts with filter type 0 (none).
    std::vector<std::uint8_t> raw;
    raw.reserve((rowBytes + 1) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        const auto row = rgb.subspan(y * rowBytes, rowBytes);
        raw.insert(raw.end(), row.begin(), row.end());
    }

    // zlib stream of stored (uncompressed) deflate blocks: fast, and the files are only for inspection.
    constexpr std::size_t kMaxBlock = 65535;
    std::vector<std::uint8_t> zlib{0x78, 0x01};
    zlib.reserve(raw.size() + raw.size() / kMaxBlock * 5 + 16);
    for (std::size_t offset = 0; offset < raw.size() || offset == 0; offset += kMaxBlock) {
        const std::size_t length = std::min(kMaxBlock, raw.size() - offset);
        const bool last = offset + length >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<std::uint8_t>(length));
        zlib.push_back(static_cast<std::uint8_t>(length >> 8));
        zlib.push_back(static_cast<std::uint8_t>(~length));
        zlib.push_back(static_cast<std::uint8_t>(~length >> 8));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
        if (last) {
            break;
        }
    }
    AppendBigEndian(zlib, Adler32(raw));

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<std::uint8_t> header;
    AppendBigEndian(header, width);
    AppendBigEndian(header, height);
    header.insert(header.end(), {8, 2, 0, 0, 0});  // 8 bits per channel, RGB, deflate, no filter, no interlace
    AppendChunk(png, "IHDR", header);
    AppendChunk(png, "IDAT", zlib);
    AppendChunk(png, "IEND", {});
    return png;
}

std::uint32_t Crc32(std::span<const std::uint8_t> bytes, std::uint32_t crc) noexcept
{
    std::uint32_t c = ~crc;
    for (const std::uint8_t byte : bytes) {
        c = kCrcTable[(c ^ byte) & 0xff] ^ (c >> 8);
    }
    return ~c;
}

std::uint32_t Adler32(std::span<const std::uint8_t> bytes) noexcept
{
    constexpr std::uint32_t kModulus = 65521;
    constexpr std::size_t kBlock = 5552;  // largest run whose sums cannot overflow 32 bits
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    while (!bytes.empty()) {
        const std::size_t length = std::min(kBlock, bytes.size());
        for (const std::uint8_t byte : bytes.first(length)) {
            a += byte;
            b += a;
        }
        a %= kModulus;
        b %= kModulus;
        bytes = bytes.subspan(length);
    }
    return (b << 16) | a;
}

}  // namespace bw::diagnostics
