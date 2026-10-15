#include "diagnostics/Screenshot.h"

#include "core/Bitmap.h"
#include "core/Log.h"
#include "core/Module.h"

#include <ddraw.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <bit>
#include <filesystem>
#include <format>
#include <cstring>
#include <string>

namespace bw::diagnostics {

namespace {

HANDLE g_requestEvent = nullptr;
int g_screenshotKey = 0;
std::atomic<bool> g_keyWasDown = false;

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
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const std::wstring name = std::format(L"BW_{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}.png", now.wYear, now.wMonth,
                                          now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return ModuleDirectory(nullptr) / L"Screenshots" / name;
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
    Microsoft::WRL::ComPtr<IDirectDrawSurface7> surface7;
    if (FAILED(surface->QueryInterface(IID_IDirectDrawSurface7, reinterpret_cast<void**>(surface7.GetAddressOf())))) {
        log::Warning("Screenshot: the back buffer has no IDirectDrawSurface7 interface");
        return;
    }
    DDSURFACEDESC2 description{};
    description.dwSize = sizeof description;
    if (FAILED(surface7->GetSurfaceDesc(&description))) {
        log::Warning("Screenshot: cannot describe the back buffer");
        return;
    }
    const PixelFormat format{description.ddpfPixelFormat.dwRGBBitCount, description.ddpfPixelFormat.dwRBitMask,
                             description.ddpfPixelFormat.dwGBitMask, description.ddpfPixelFormat.dwBBitMask};
    if (format.bitsPerPixel != 16 && format.bitsPerPixel != 32) {
        log::Warning("Screenshot: {}-bit surfaces are not supported", format.bitsPerPixel);
        return;
    }
    if (description.dwWidth == 0 || description.dwHeight == 0 || description.dwWidth > kMaxBitmapSide ||
        description.dwHeight > kMaxBitmapSide) {
        return;
    }
    Bitmap bitmap;
    bitmap.width = description.dwWidth;
    bitmap.height = description.dwHeight;
    try {
        bitmap.argb.resize(std::size_t{bitmap.width} * bitmap.height);
    }
    catch (const std::bad_alloc&) {
        log::Warning("Screenshot: out of memory");
        return;
    }
    if (const HRESULT result = surface7->Lock(nullptr, &description,
                                              DDLOCK_WAIT | DDLOCK_READONLY | DDLOCK_SURFACEMEMORYPTR, nullptr);
        FAILED(result)) {
        log::Warning("Screenshot: cannot read the back buffer (0x{:08x})", static_cast<std::uint32_t>(result));
        return;
    }
    const auto* rows = static_cast<const std::uint8_t*>(description.lpSurface);
    for (std::uint32_t y = 0; y < bitmap.height; ++y) {
        const std::uint8_t* row = rows + std::size_t{y} * description.lPitch;
        std::uint32_t* out = bitmap.argb.data() + std::size_t{y} * bitmap.width;
        for (std::uint32_t x = 0; x < bitmap.width; ++x) {
            std::uint32_t pixel = 0;
            if (format.bitsPerPixel == 32) {
                std::memcpy(&pixel, row + std::size_t{x} * 4, 4);
            }
            else {
                std::uint16_t value = 0;
                std::memcpy(&value, row + std::size_t{x} * 2, 2);
                pixel = value;
            }
            out[x] = 0xff000000u | std::uint32_t{ExtractChannel(pixel, format.redMask)} << 16 |
                     std::uint32_t{ExtractChannel(pixel, format.greenMask)} << 8 |
                     ExtractChannel(pixel, format.blueMask);
        }
    }
    surface7->Unlock(nullptr);

    try {
        const std::filesystem::path path = ScreenshotPath();
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (!WritePng(path, bitmap)) {
            log::Warning("Screenshot: cannot write {}", log::PathText(path));
            return;
        }
        log::Info("Screenshot: {}", log::PathText(path));
    }
    catch (const std::exception& error) {
        log::Warning("Screenshot failed: {}", error.what());
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

}  // namespace bw::diagnostics
