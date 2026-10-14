#pragma once

// Lionhead library display layer (C:\DEV\Libs\LIONHEAD\lhlib\VER5.0\LHScreen.cpp). One global instance at
// 0x00d5c100 owns the DirectDraw object, the primary and back surfaces and the game window.

#include "core/Layout.h"
#include "core/Memory.h"
#include "core/Windows.h"

#include <cstddef>
#include <cstdint>

struct IDirectDraw;
struct IDirectDrawSurface;

namespace lhlib {

/// Partially recovered. Members named unknownXX are untouched by decompiled code so far; their offsets are kept so
/// that recovered members stay at the original positions.
class LHScreen {
public:
    /// Free local video memory in bytes, clamped to [0, MaxReportedVideoMemoryMB]. PCMain enables the low-detail
    /// texture path when this is below 13,000,000 and LH3DRender logs it.
    [[nodiscard]] std::int32_t GetAvailableVideoMemory() noexcept;  // 0x00749a70

    [[nodiscard]] std::uint16_t Width() const noexcept { return m_width; }
    [[nodiscard]] std::uint16_t Height() const noexcept { return m_height; }
    [[nodiscard]] std::uint8_t BitsPerPixel() const noexcept { return m_bitsPerPixel; }
    [[nodiscard]] HWND Window() const noexcept { return m_window; }

private:
    friend struct LHScreenLayout;

    std::byte m_unknown00[4];                  // 0x00
    std::int32_t m_modeSet;                    // 0x04  set to 1 by SetMode (0x00748d70)
    std::uint16_t m_width;                     // 0x08
    std::uint16_t m_height;                    // 0x0a
    std::int32_t m_pitchBytes;                 // 0x0c
    std::int32_t m_pitchPixels;                // 0x10
    std::uint8_t m_bitsPerPixel;               // 0x14
    std::byte m_unknown15[0x28 - 0x15];        // 0x15
    HWND m_window;                             // 0x28
    IDirectDraw* m_directDraw;                 // 0x2c
    IDirectDrawSurface* m_primarySurface;      // 0x30
    IDirectDrawSurface* m_backSurface;         // 0x34
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
};

BW_GLOBAL(LHScreen, g_screen, 0x00d5c100);

/// Clamps a DirectDraw byte count to what the game's signed 32-bit arithmetic can hold and to `limitMiB`
/// (0 = no limit beyond INT32_MAX).
[[nodiscard]] std::int32_t ClampVideoMemory(std::uint64_t bytes, std::uint32_t limitMiB) noexcept;

}  // namespace lhlib
