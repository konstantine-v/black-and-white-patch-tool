#pragma once

// Shared pieces of the DirectDraw/Direct3D 7 replacement objects (translation units that include ddraw.h and d3d.h).

#include "core/Windows.h"
#include "render/Format.h"

#include <ddraw.h>
#include <d3d.h>

#include <atomic>

namespace bw::render {

/// Logs the first call of a method the replacement does not implement and returns DDERR_UNSUPPORTED. Every such
/// method is listed in docs/RENDERER.md; a call means the game needs it after all.
HRESULT Unsupported(const char* method, bool& logged) noexcept;

#define BW_UNSUPPORTED                                                                                             \
    {                                                                                                              \
        static bool logged_ = false;                                                                               \
        return ::bw::render::Unsupported(__FUNCTION__, logged_);                                                   \
    }

/// Reference count shared by an object's interfaces (all versions of a COM object count together).
class RefCount {
public:
    ULONG AddReference() noexcept { return ++m_count; }
    /// Returns the new count; the caller deletes the object at 0.
    ULONG ReleaseReference() noexcept { return --m_count; }

private:
    std::atomic<ULONG> m_count{1};
};

[[nodiscard]] Format FromPixelFormat(const DDPIXELFORMAT& format) noexcept;
[[nodiscard]] DDPIXELFORMAT ToPixelFormat(Format format) noexcept;

/// Copies the fields of a DirectX 6/7 surface description that an older DDSURFACEDESC also has.
void ToSurfaceDesc(const DDSURFACEDESC2& from, DDSURFACEDESC& to) noexcept;
[[nodiscard]] DDSURFACEDESC2 FromSurfaceDesc(const DDSURFACEDESC& from) noexcept;

}  // namespace bw::render
