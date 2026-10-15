#include "game/lhlib/LHScreen.h"

#include "core/Config.h"
#include "core/Hook.h"
#include "core/Log.h"
#include "diagnostics/DirectXTrace.h"
#include "diagnostics/Screenshot.h"
#include "game/lh3dlib/LH3DRender.h"
#include "render/Renderer.h"

#include <ddraw.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace lhlib {

namespace {

// Not decompiled yet.
inline constexpr bw::ThiscallFunction<0x00748500, std::int32_t(LHScreen*)> InitialiseDirectDraw{};
inline constexpr bw::ThiscallFunction<0x00749430, void(LHScreen*, HWND)> AttachWindow{};
inline constexpr bw::CdeclFunction<0x0074bbf0, void()> RestoreFonts{};
inline constexpr bw::ThiscallFunction<0x00748de0, std::int32_t(LHScreen*, std::int32_t, std::int32_t, std::int32_t)>
    ApplyMode{};
// LH3DRender::Close / LH3DRender::Start, called through their jump thunks as the original does.
inline constexpr bw::CdeclFunction<0x007cc7e0, void()> CloseRenderer{};
inline constexpr bw::CdeclFunction<0x007cc7d0, std::int32_t()> StartRenderer{};
inline constexpr bw::ThiscallFunction<0x00748b30, std::int32_t(LHScreen*, std::int32_t)> LockOriginal{};
inline constexpr bw::ThiscallFunction<0x00748c00, std::int32_t(LHScreen*)> UnlockOriginal{};
inline constexpr bw::ThiscallFunction<0x007497a0, void(LHScreen*)> RestoreSurfacesOriginal{};
inline constexpr bw::ThiscallFunction<0x00749340, std::int32_t(LHScreen*, std::int32_t, std::int32_t)>
    AfterFlip00749340{};
/// Removes the window's menu bar (LHWin.cpp).
inline constexpr bw::CdeclFunction<0x00746ea0, void()> RemoveWindowMenu{};
/// LHLogR.dll: when set, failed assertions are logged instead of shown in a dialog.
BW_IMPORTED_DATA(std::int32_t, LHAssertIgnoreAllAsserts, 0x007eb32c);
/// SELFMOD (0x00ee0285): patches the software rasteriser's four instructions that address the back buffer.
inline constexpr bw::CdeclFunction<0x00ee0285, void(void*)> SetRasteriserTarget{};

constexpr std::int32_t kSetModeFailed = 3;
constexpr std::size_t kMaxFallbackAttempts = 6;

/// Every display mode of the primary display at `bitsPerPixel`.
std::vector<DisplayMode> AvailableModes(std::int32_t bitsPerPixel)
{
    std::vector<DisplayMode> modes;
    DEVMODEW mode{};
    mode.dmSize = sizeof mode;
    for (DWORD index = 0; EnumDisplaySettingsW(nullptr, index, &mode); ++index) {
        if (static_cast<std::int32_t>(mode.dmBitsPerPel) != bitsPerPixel) {
            continue;
        }
        const DisplayMode candidate{static_cast<std::int32_t>(mode.dmPelsWidth),
                                    static_cast<std::int32_t>(mode.dmPelsHeight)};
        if (std::ranges::find(modes, candidate) == modes.end()) {
            modes.push_back(candidate);
        }
    }
    return modes;
}

}  // namespace

std::vector<DisplayMode> RankFallbackModes(std::span<const DisplayMode> available, DisplayMode requested)
{
    if (requested.width <= 0 || requested.height <= 0) {
        return {};
    }
    const double requestedAspect = static_cast<double>(requested.width) / requested.height;
    auto sameAspect = [&](const DisplayMode& mode) {
        return std::abs(static_cast<double>(mode.width) / mode.height - requestedAspect) < 0.01;
    };
    std::vector<DisplayMode> ranked;
    for (const DisplayMode& mode : available) {
        if (mode.width > 0 && mode.height > 0 && mode.Area() < requested.Area() && mode.width <= requested.width &&
            mode.height <= requested.height) {
            ranked.push_back(mode);
        }
    }
    std::ranges::sort(ranked, [&](const DisplayMode& a, const DisplayMode& b) {
        if (sameAspect(a) != sameAspect(b)) {
            return sameAspect(a);
        }
        return a.Area() > b.Area();
    });
    return ranked;
}

RECT FitFrame(DisplayMode frame, DisplayMode client, bool keepAspectRatio) noexcept
{
    if (!keepAspectRatio || frame.width <= 0 || frame.height <= 0) {
        return {0, 0, client.width, client.height};
    }
    // Width-limited when client.width / client.height <= frame.width / frame.height.
    std::int64_t width = client.width;
    std::int64_t height = client.height;
    if (std::int64_t{client.width} * frame.height <= std::int64_t{client.height} * frame.width) {
        height = std::int64_t{client.width} * frame.height / frame.width;
    }
    else {
        width = std::int64_t{client.height} * frame.width / frame.height;
    }
    const auto left = static_cast<LONG>((client.width - width) / 2);
    const auto top = static_cast<LONG>((client.height - height) / 2);
    return {left, top, left + static_cast<LONG>(width), top + static_cast<LONG>(height)};
}

POINT MapToFrame(POINT client, const RECT& frameRect, DisplayMode frame) noexcept
{
    const std::int64_t width = frameRect.right - frameRect.left;
    const std::int64_t height = frameRect.bottom - frameRect.top;
    if (width <= 0 || height <= 0 || frame.width <= 0 || frame.height <= 0) {
        return {0, 0};
    }
    const std::int64_t x = std::clamp<std::int64_t>(client.x - frameRect.left, 0, width - 1) * frame.width / width;
    const std::int64_t y = std::clamp<std::int64_t>(client.y - frameRect.top, 0, height - 1) * frame.height / height;
    return {static_cast<LONG>(x), static_cast<LONG>(y)};
}

POINT MapFromFrame(POINT framePoint, const RECT& frameRect, DisplayMode frame) noexcept
{
    if (frame.width <= 0 || frame.height <= 0) {
        return {frameRect.left, frameRect.top};
    }
    // The centre of the frame pixel, so that MapToFrame(MapFromFrame(p)) == p.
    const std::int64_t width = frameRect.right - frameRect.left;
    const std::int64_t height = frameRect.bottom - frameRect.top;
    const std::int64_t x = (2 * std::int64_t{framePoint.x} + 1) * width / (2 * std::int64_t{frame.width});
    const std::int64_t y = (2 * std::int64_t{framePoint.y} + 1) * height / (2 * std::int64_t{frame.height});
    return {frameRect.left + static_cast<LONG>(x), frameRect.top + static_cast<LONG>(y)};
}

bool LHScreen::Scaled() const noexcept
{
    return m_windowed != 0 && bw::GetConfig().windowMode != bw::WindowMode::FullScreen;
}

RECT LHScreen::FrameRect() const noexcept
{
    const RECT unscaled{0, 0, m_width, m_height};
    RECT client{};
    if (!Scaled() || !GetClientRect(m_window, &client) || client.right <= 0 || client.bottom <= 0) {
        return unscaled;
    }
    return FitFrame(Size(), {client.right, client.bottom}, bw::GetConfig().keepAspectRatio);
}

POINT LHScreen::ClientToFrame(POINT client) const noexcept
{
    return Scaled() ? MapToFrame(client, FrameRect(), Size()) : client;
}

POINT LHScreen::FrameToClient(POINT framePoint) const noexcept
{
    return Scaled() ? MapFromFrame(framePoint, FrameRect(), Size()) : framePoint;
}

std::int32_t LHScreen::SetMode(std::int32_t width, std::int32_t height, std::int32_t bitsPerPixel) noexcept
{
    if (g_screenNeedsInitialise != 0) {
        g_screenNeedsInitialise = 0;
        if (const std::int32_t error = InitialiseDirectDraw(this); error != 0) {
            return error;
        }
        AttachWindow(this, g_mainWindow);
        RestoreFonts();
    }

    const bw::Config& config = bw::GetConfig();
    const std::int32_t depth = EffectiveColorDepth(bitsPerPixel, config.promoteColorDepth16To32);
    if (depth != bitsPerPixel) {
        bw::log::Info("LHScreen: {}x{} requested at {} bits, using {} bits", width, height, bitsPerPixel, depth);
    }

    CloseRenderer();
    std::int32_t result = ApplyMode(this, width, height, depth);
    if (result == kSetModeFailed) {
        bw::log::Error("LHScreen: cannot set {}x{}x{}", width, height, depth);
        return kSetModeFailed;
    }
    m_modeSet = 1;
    if (StartRenderer() != 0 || lh3dlib::g_renderEnabled == 0 || !config.displayModeFallback) {
        return result;
    }

    // Direct3D refused this mode (current drivers reject render targets wider than 2048 pixels). Step down
    // through the display's smaller modes, same aspect ratio first, until the renderer starts.
    try {
        const std::vector<DisplayMode> fallbacks = RankFallbackModes(AvailableModes(depth), {width, height});
        for (std::size_t attempt = 0; attempt < std::min(fallbacks.size(), kMaxFallbackAttempts); ++attempt) {
            const DisplayMode& mode = fallbacks[attempt];
            bw::log::Warning("LHScreen: trying {}x{} instead of {}x{}", mode.width, mode.height, width, height);
            result = ApplyMode(this, mode.width, mode.height, depth);
            if (result != kSetModeFailed && StartRenderer() != 0) {
                bw::log::Info("LHScreen: running at {}x{}x{}", mode.width, mode.height, depth);
                return result;
            }
        }
    }
    catch (const std::bad_alloc&) {
    }
    bw::log::Error("LHScreen: no display mode with a Direct3D device");
    return result;
}

std::int32_t ClampVideoMemory(std::uint64_t bytes, std::uint32_t limitMiB) noexcept
{
    std::uint64_t limit = std::numeric_limits<std::int32_t>::max();
    if (limitMiB != 0) {
        limit = std::min<std::uint64_t>(limit, std::uint64_t{limitMiB} << 20);
    }
    return static_cast<std::int32_t>(std::min(bytes, limit));
}

std::int32_t LHScreen::GetAvailableVideoMemory() noexcept
{
    // Original: QueryInterface for IDirectDraw2 (never released), GetAvailableVidMem(DDSCAPS_VIDEOMEMORY) and return
    // the free byte count as a signed int, uninitialised if the call failed. On current hardware the count wraps
    // negative (or is garbage), so PCMain switched every large card to low-detail textures.
    if (m_directDraw == nullptr) {
        return 0;
    }
    IDirectDraw2* directDraw2 = nullptr;
    if (FAILED(m_directDraw->QueryInterface(IID_IDirectDraw2, reinterpret_cast<void**>(&directDraw2)))) {
        bw::log::Warning("LHScreen: IDirectDraw2 unavailable; reporting no video memory");
        return 0;
    }
    DDSCAPS caps{};
    caps.dwCaps = DDSCAPS_VIDEOMEMORY;
    DWORD total = 0;
    DWORD free = 0;
    const HRESULT result = directDraw2->GetAvailableVidMem(&caps, &total, &free);
    directDraw2->Release();
    if (FAILED(result)) {
        bw::log::Warning("LHScreen: GetAvailableVidMem failed (0x{:08x})", static_cast<std::uint32_t>(result));
        return 0;
    }
    return ClampVideoMemory(free, bw::GetConfig().maxReportedVideoMemoryMiB);
}

void LHScreen::SetFullScreen(std::int32_t fullScreen) noexcept
{
    if (bw::GetConfig().windowMode != bw::WindowMode::FullScreen) {
        // The original only set m_windowed; dialogs then stayed enabled, which a retail build never expected.
        LHAssertIgnoreAllAsserts() = 1;
        m_windowed = 1;
        return;
    }
    if (fullScreen == 0) {
        m_windowed = 1;
        return;
    }
    LHAssertIgnoreAllAsserts() = 1;
    m_windowed = 0;
    SetWindowLongA(m_window, GWL_STYLE, static_cast<LONG>(WS_POPUP));
    SetWindowLongA(m_window, GWL_EXSTYLE, WS_EX_TOPMOST);
    RemoveWindowMenu();
}

std::int32_t LHScreen::Flip(std::int32_t swapBuffers) noexcept
{
    bw::diagnostics::RefreshDirectXTrace();
    if (bw::log::DebugEnabled()) {
        static ULONGLONG start = GetTickCount64();
        static std::uint32_t frames = 0;
        ++frames;
        if (const ULONGLONG elapsed = GetTickCount64() - start; elapsed >= 10'000) {
            bw::log::Debug("LHScreen: {:.1f} frames per second", frames * 1000.0 / static_cast<double>(elapsed));
            if (bw::render::Active()) {
                const auto statistics = bw::render::gpu::TakeStatistics();
                bw::log::Debug("  per frame: {:.0f} draws, {:.2f} texture uploads, {:.2f} depth reads, {:.2f} depth "
                               "probes, {:.2f} resolves, {:.2f} frame reads, {:.2f} frame writes",
                               statistics.draws / double(frames), statistics.textureUploads / double(frames),
                               statistics.depthReads / double(frames), statistics.depthProbes / double(frames),
                               statistics.resolves / double(frames), statistics.targetReads / double(frames),
                               statistics.targetWrites / double(frames));
            }
            frames = 0;
            start = GetTickCount64();
        }
    }
    if (m_locked == 0 && bw::diagnostics::ScreenshotRequested()) {
        bw::diagnostics::SaveScreenshot(m_backSurface);
    }

    if (m_windowed != 0 && Scaled()) {
        PresentScaled();
    }
    else if (m_windowed != 0) {
        RECT target{m_windowX, m_windowY, m_windowX + m_width, m_windowY + m_height};
        m_primarySurface->Blt(&target, m_backSurface, nullptr, DDBLT_WAIT, nullptr);
    }
    else {
        m_primarySurface->Flip(nullptr, m_flipFlags);
    }

    if (swapBuffers != 0) {
        if (m_windowed == 0) {
            // The flip exchanged the surfaces' memory: keep the software rasteriser drawing into the back buffer.
            std::swap(m_frontPixels, m_backPixels);
            std::swap(m_pitchPixels, m_backPitchPixels);
            SetRasteriserTarget(m_backPixels);
        }
        AfterFlip00749340(this, 0, 0);
    }
    // Refreshes the software rasteriser's pointer to the back buffer. The rasteriser is dead code in this build
    // (nothing calls into SELFMOD except the pointer update itself), so the Direct3D 9 renderer skips the lock, which
    // would read the frame back from the GPU.
    if (!bw::render::Active() && Lock(0) == 0) {
        Unlock();
    }
    return 0;
}

void LHScreen::PresentScaled() noexcept
{
    RECT client{};
    if (!GetClientRect(m_window, &client) || client.right <= 0 || client.bottom <= 0) {
        return;  // minimised
    }
    POINT origin{0, 0};
    ClientToScreen(m_window, &origin);
    auto toScreen = [&](RECT rect) {
        OffsetRect(&rect, origin.x, origin.y);
        return rect;
    };

    const RECT frame = FrameRect();
    // Clear the bars around a letterboxed picture; the primary surface's clipper keeps this inside the window.
    const RECT bars[] = {
        {0, 0, client.right, frame.top},
        {0, frame.bottom, client.right, client.bottom},
        {0, frame.top, frame.left, frame.bottom},
        {frame.right, frame.top, client.right, frame.bottom},
    };
    DDBLTFX fill{};
    fill.dwSize = sizeof fill;
    fill.dwFillColor = 0;
    for (const RECT& bar : bars) {
        if (bar.right > bar.left && bar.bottom > bar.top) {
            RECT target = toScreen(bar);
            m_primarySurface->Blt(&target, nullptr, nullptr, DDBLT_COLORFILL | DDBLT_WAIT, &fill);
        }
    }
    RECT target = toScreen(frame);
    m_primarySurface->Blt(&target, m_backSurface, nullptr, DDBLT_WAIT, nullptr);
}

std::int32_t LHScreen::Lock(std::int32_t unknown) noexcept
{
    return LockOriginal(this, unknown);
}

std::int32_t LHScreen::Unlock() noexcept
{
    return UnlockOriginal(this);
}

void LHScreen::RestoreSurfaces() noexcept
{
    RestoreSurfacesOriginal(this);
}

BW_HOOK_METHOD(0x00748d70, LHScreen, SetMode);
BW_HOOK_METHOD(0x007496c0, LHScreen, Flip);
BW_HOOK_METHOD(0x007484a0, LHScreen, SetFullScreen);
BW_HOOK_METHOD(0x00749a70, LHScreen, GetAvailableVideoMemory);

}  // namespace lhlib
