#pragma once

// Lionhead 3D library renderer (C:\dev\Libs\LIONHEAD\LH3DLIB\DEV\LH3DRender.cpp). Direct3D 7 on top of LHScreen's
// DirectDraw surfaces. Partially decompiled; unknown globals keep their address in the name.

#include "core/Memory.h"
#include "core/Windows.h"

#include <d3d.h>

#include <cstdint>

namespace lh3dlib {

struct LH3DRender {
    /// Opens the renderer for the current display mode: Direct3D device, z-buffer, render states and the
    /// subsystems that depend on them. Returns 1 on success, 0 when disabled, already open, or (decompilation fix)
    /// when no Direct3D device could be created; the original carried on and crashed on the null device.
    [[nodiscard]] static std::int32_t Start() noexcept;  // 0x00784520

    /// Reports why Direct3D refused the current back buffer by repeating the HAL CreateDevice call.
    static void LogDeviceFailure() noexcept;

    /// Releases what a failed Direct3D start-up created. CloseDirect3D (0x00786640) cannot be used: its texture
    /// clean-up assumes a working device.
    static void ReleaseFailedStart() noexcept;
};

/// IDirect3D7 obtained from LHScreen's DirectDraw object by the Direct3D start-up (0x00785fd0).
BW_GLOBAL(IDirect3D7*, g_direct3D, 0x00de9490);
/// The rendering device; null when CreateDevice failed.
BW_GLOBAL(IDirect3DDevice7*, g_device, 0x00de9494);
/// The z-buffer surface attached to LHScreen's back buffer.
BW_GLOBAL(IUnknown*, g_zBuffer, 0x00de9498);

/// Set by PCMain at start-up; the renderer does nothing while 0.
BW_GLOBAL(std::int32_t, g_renderEnabled, 0x00de9480);
/// 1 between Start and Close.
BW_GLOBAL(std::int32_t, g_renderOpen, 0x00de947c);
/// 1 when a 16-bit z-buffer was chosen (logged by Start).
BW_GLOBAL(std::int32_t, g_zBuffer16, 0x00b5a760);
/// PCMain's low-video-memory flag (free video memory below 13,000,000 bytes): halves the cache below.
BW_GLOBAL(std::int32_t, g_lowVideoMemory, 0x00df820c);

/// Statistics block reset by the first Start and checked by Close.
struct RenderStatistics {
    std::int32_t counters[4];
    std::int32_t initialised;
};
BW_GLOBAL(RenderStatistics, g_renderStatistics, 0x00e153a0);

/// A cache of 256 (128 with low video memory) 28-byte entries; Start clears the first word of each.
struct RenderCacheEntry {
    std::int32_t inUse;
    std::byte unknown04[24];
};
static_assert(sizeof(RenderCacheEntry) == 0x1c);
BW_GLOBAL(RenderCacheEntry, g_renderCache, 0x00ec7530);  // first of 256
BW_GLOBAL(std::int32_t, g_renderCacheSize, 0x00b5b878);
BW_GLOBAL(float, g_renderCacheLast, 0x00b5b87c);

}  // namespace lh3dlib
