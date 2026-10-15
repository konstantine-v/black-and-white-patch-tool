#pragma once

#include "core/Config.h"
#include "core/Windows.h"

#include <cstdint>
#include <span>
#include <vector>

struct IDirectDrawSurface;

/// Screenshots of the game's back buffer. Full-screen DirectDraw cannot be captured from outside (GDI and Print
/// Screen see black), so LHScreen::Flip saves the frame itself when a screenshot is requested: by the configured key
/// (ScreenshotKey) or by signalling the event `Local\BWDecomp.Screenshot.<process id>` (tools/screenshot.py).
/// Files go to `Screenshots\` in the game folder as PNG.
namespace bw::diagnostics {

void InstallScreenshots(const Config& config) noexcept;

/// True once per request; polled by the frame flip.
[[nodiscard]] bool ScreenshotRequested() noexcept;

/// Saves the surface (16- or 32-bit RGB) as a PNG and logs the file name.
void SaveScreenshot(IDirectDrawSurface* surface) noexcept;

/// Pixel layout of a locked surface: the masks of the colour channels in a 16- or 32-bit pixel.
struct PixelFormat {
    std::uint32_t bitsPerPixel = 32;
    std::uint32_t redMask = 0x00ff0000;
    std::uint32_t greenMask = 0x0000ff00;
    std::uint32_t blueMask = 0x000000ff;
};

/// One channel scaled to 8 bits (a 5-bit 31 becomes 255).
[[nodiscard]] std::uint8_t ExtractChannel(std::uint32_t pixel, std::uint32_t mask) noexcept;

}  // namespace bw::diagnostics
