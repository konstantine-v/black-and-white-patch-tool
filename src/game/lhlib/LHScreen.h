#pragma once

// Lionhead library display layer (C:\DEV\Libs\LIONHEAD\lhlib\VER5.0\LHScreen.cpp). One global instance at
// 0x00d5c100 owns the DirectDraw object, the primary and back surfaces and the game window.

#include "core/Layout.h"
#include "core/Memory.h"
#include "core/Windows.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

struct IDirectDraw;
struct IDirectDrawSurface;

namespace lhlib {

struct DisplayMode {
    std::int32_t width = 0;
    std::int32_t height = 0;

    [[nodiscard]] constexpr std::int64_t Area() const noexcept { return std::int64_t{width} * height; }
    friend constexpr bool operator==(const DisplayMode&, const DisplayMode&) = default;
};

/// Modes to try when Direct3D refuses `requested`: smaller in both dimensions, same aspect ratio first, then by
/// decreasing area.
[[nodiscard]] std::vector<DisplayMode> RankFallbackModes(std::span<const DisplayMode> available, DisplayMode requested);

/// Partially recovered. Members named unknownXX are untouched by decompiled code so far; their offsets are kept so
/// that recovered members stay at the original positions.
class LHScreen {
public:
    /// Closes LH3D, switches the display mode and restarts LH3D. Returns 0 on success, 3 when the mode could not be
    /// set, or the error of the one-time DirectDraw initialisation. Fixes: 16-bit requests run at 32 bits
    /// (ColorDepth32); when Direct3D refuses the mode, smaller modes are tried (DisplayModeFallback).
    [[nodiscard]] std::int32_t SetMode(std::int32_t width, std::int32_t height,
                                       std::int32_t bitsPerPixel) noexcept;  // 0x00748d70

    /// Free local video memory in bytes, clamped to [0, MaxReportedVideoMemoryMB]. PCMain enables the low-detail
    /// texture path when this is below 13,000,000 and LH3DRender logs it.
    [[nodiscard]] std::int32_t GetAvailableVideoMemory() noexcept;  // 0x00749a70

    /// Shows the back buffer: Flip in full-screen mode, Blt into the window's client area in windowed mode. With
    /// `swapBuffers`, the software rasteriser is pointed at the new back buffer. Always returns 0. Diagnostics:
    /// saves a screenshot of the frame when one was requested (diagnostics/Screenshot.h).
    std::int32_t Flip(std::int32_t swapBuffers) noexcept;  // 0x007496c0

    /// Locks the back surface for the software rasteriser and records its address and pitch. Returns 0 on success,
    /// 2 when already locked or the surface was lost, 3 on other DirectDraw errors. (Original, not decompiled.)
    std::int32_t Lock(std::int32_t unknown) noexcept;  // 0x00748b30
    /// Unlocks the back surface; returns 0 on success, 2 when it was not locked or Unlock failed. (Original.)
    std::int32_t Unlock() noexcept;  // 0x00748c00
    /// Restores the surfaces after the game window was minimised and ends the suspension. (Original.)
    void RestoreSurfaces() noexcept;  // 0x007497a0

    [[nodiscard]] std::uint16_t Width() const noexcept { return m_width; }
    [[nodiscard]] std::uint16_t Height() const noexcept { return m_height; }
    [[nodiscard]] std::uint8_t BitsPerPixel() const noexcept { return m_bitsPerPixel; }
    [[nodiscard]] HWND Window() const noexcept { return m_window; }
    [[nodiscard]] IDirectDrawSurface* BackSurface() const noexcept { return m_backSurface; }
    [[nodiscard]] bool Windowed() const noexcept { return m_windowed != 0; }

private:
    friend struct LHScreenLayout;

    std::byte m_unknown00[4];                  // 0x00
    std::int32_t m_modeSet;                    // 0x04  set to 1 by SetMode (0x00748d70)
    std::uint16_t m_width;                     // 0x08
    std::uint16_t m_height;                    // 0x0a
    std::int32_t m_pitchBytes;                 // 0x0c
    std::int32_t m_pitchPixels;                // 0x10  front buffer; swapped with m_backPitchPixels by Flip
    std::uint8_t m_bitsPerPixel;               // 0x14
    std::byte m_unknown15[3];                  // 0x15
    std::int32_t m_locked;                     // 0x18  back surface locked by Lock
    void* m_frontPixels;                       // 0x1c
    std::byte m_unknown20[4];                  // 0x20
    void* m_backPixels;                        // 0x24  where the software rasteriser draws (set by Lock)
    HWND m_window;                             // 0x28
    IDirectDraw* m_directDraw;                 // 0x2c
    IDirectDrawSurface* m_primarySurface;      // 0x30
    IDirectDrawSurface* m_backSurface;         // 0x34
    std::byte m_unknown38[0x58 - 0x38];        // 0x38
    std::int32_t m_windowX;                    // 0x58  screen position of the client area in windowed mode
    std::int32_t m_windowY;                    // 0x5c
    std::byte m_unknown60[4];                  // 0x60
    std::int32_t m_windowed;                   // 0x64
    std::byte m_unknown68[0x84 - 0x68];        // 0x68
    DWORD m_flipFlags;                         // 0x84
    std::byte m_unknown88[4];                  // 0x88
    std::int32_t m_backPitchPixels;            // 0x8c
};

struct LHScreenLayout {
    BW_ASSERT_OFFSET(LHScreen, m_modeSet, 0x04);
    BW_ASSERT_OFFSET(LHScreen, m_width, 0x08);
    BW_ASSERT_OFFSET(LHScreen, m_height, 0x0a);
    BW_ASSERT_OFFSET(LHScreen, m_pitchBytes, 0x0c);
    BW_ASSERT_OFFSET(LHScreen, m_pitchPixels, 0x10);
    BW_ASSERT_OFFSET(LHScreen, m_bitsPerPixel, 0x14);
    BW_ASSERT_OFFSET(LHScreen, m_window, 0x28);
    BW_ASSERT_OFFSET(LHScreen, m_directDraw, 0x2c);
    BW_ASSERT_OFFSET(LHScreen, m_primarySurface, 0x30);
    BW_ASSERT_OFFSET(LHScreen, m_backSurface, 0x34);
    BW_ASSERT_OFFSET(LHScreen, m_locked, 0x18);
    BW_ASSERT_OFFSET(LHScreen, m_frontPixels, 0x1c);
    BW_ASSERT_OFFSET(LHScreen, m_backPixels, 0x24);
    BW_ASSERT_OFFSET(LHScreen, m_windowX, 0x58);
    BW_ASSERT_OFFSET(LHScreen, m_windowY, 0x5c);
    BW_ASSERT_OFFSET(LHScreen, m_windowed, 0x64);
    BW_ASSERT_OFFSET(LHScreen, m_flipFlags, 0x84);
    BW_ASSERT_OFFSET(LHScreen, m_backPitchPixels, 0x8c);
};

BW_GLOBAL(LHScreen, g_screen, 0x00d5c100);
/// Set until the first SetMode has initialised DirectDraw for the game window.
BW_GLOBAL(std::int32_t, g_screenNeedsInitialise, 0x00b53800);
BW_GLOBAL(HWND, g_mainWindow, 0x00d62f24);
/// 1 while the surfaces are suspended: set when the window is deactivated (0x00749780), cleared by RestoreSurfaces.
BW_GLOBAL(std::int32_t, g_screenSuspended, 0x00d63410);

/// The colour depth SetMode actually uses: 16-bit requests become 32-bit when `promote16To32` is set, because
/// current drivers no longer create a hardware Direct3D 7 device on a 16-bit display mode.
[[nodiscard]] constexpr std::int32_t EffectiveColorDepth(std::int32_t requested, bool promote16To32) noexcept
{
    return promote16To32 && requested == 16 ? 32 : requested;
}

/// Clamps a DirectDraw byte count to what the game's signed 32-bit arithmetic can hold and to `limitMiB`
/// (0 = no limit beyond INT32_MAX).
[[nodiscard]] std::int32_t ClampVideoMemory(std::uint64_t bytes, std::uint32_t limitMiB) noexcept;

}  // namespace lhlib
