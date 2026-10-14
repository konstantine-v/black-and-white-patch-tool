#include "game/lh3dlib/LH3DSky.h"

#include "core/Call.h"
#include "core/Hook.h"
#include "game/lh3dlib/LH3DRender.h"
#include "render/Renderer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace lh3dlib {

namespace {

// Not decompiled yet.
/// Nonzero when `point` is hidden from `camera` by the landscape; the last two arguments receive values the sun test
/// does not use.
inline constexpr bw::FastcallFunction<0x00760490, std::int32_t(const Camera* camera, const Vector3* point,
                                                                std::int32_t* unused1, std::int32_t* unused2)>
    IsHiddenByLandscape{};
/// Projects `point` to the screen: 2 when it lands in front of the camera, with its pixel in `x`, `y`.
inline constexpr bw::FastcallFunction<0x00774350, std::int32_t(const Vector3* point, std::int32_t* x, std::int32_t* y)>
    ProjectToScreen{};

constexpr std::int32_t kProjectedOnScreen = 2;
/// 16-bit depth at or beyond which a pixel shows the sky.
constexpr std::uint16_t kSkyDepth = 65000;
constexpr float kMinimumHeight = 10.0f;
constexpr std::size_t kTestPoints = 5;

}  // namespace

float LH3DSky::SunVisibility() noexcept
{
    const float spread = g_sunTestSpread;
    std::array<Vector3, kTestPoints> points{{
        {0.0f, 0.0f, 0.0f},
        {-spread, -spread, 0.0f},
        {-spread, spread, 0.0f},
        {spread, -spread, 0.0f},
        {spread, spread, 0.0f},
    }};

    // Beyond the sun, away from the camera, by the near distance.
    Vector3 away{g_sunPosition.x - g_camera.position.x, g_sunPosition.y - g_camera.position.y,
                 g_sunPosition.z - g_camera.position.z};
    if (away.x != 0.0f || away.y != 0.0f || away.z != 0.0f) {
        const float scale = g_nearDistance / std::sqrt(away.z * away.z + away.y * away.y + away.x * away.x);
        away.x *= scale;
        away.y *= scale;
        away.z *= scale;
    }

    // The depth test, with a 16-bit z-buffer only (the values are read as 16-bit).
    const bool probe = bw::render::Active();
    std::int32_t depthTest = g_zBuffer16;
    HRESULT lockResult = 1;
    DDSURFACEDESC2 desc{};
    if (g_unknown00b5a2d4 == 3 || g_unknown00b5a2d4 == 4 || g_unknown00b5a2d4 == 6) {
        if (depthTest != 0) {
            if (probe) {
                lockResult = DD_OK;  // occlusion queries, no lock
            }
            else {
                desc.dwSize = sizeof desc;
                lockResult = static_cast<IDirectDrawSurface7*>(g_zBuffer)->Lock(
                    nullptr, &desc, DDLOCK_WAIT | DDLOCK_READONLY | DDLOCK_NOSYSLOCK, nullptr);
            }
        }
    }
    else {
        depthTest = 0;
    }

    float hiddenCount = 0.0f;
    for (std::size_t i = 0; i < points.size(); ++i) {
        Vector3& point = points[i];
        point.x = g_sunPosition.x + point.x;
        point.y = g_sunPosition.y + point.y;
        point.z = g_sunPosition.z + point.z;
        point.x = away.x + point.x;
        point.y = away.y + point.y;
        point.z = away.z + point.z;
        if (point.y < kMinimumHeight) {
            point.y = kMinimumHeight;
        }
        std::int32_t unused1 = 0;
        std::int32_t unused2 = 0;
        std::int32_t hidden = IsHiddenByLandscape(&g_camera, &point, &unused1, &unused2);
        if (hidden == 0 && lockResult == DD_OK && depthTest != 0) {
            std::int32_t x = 0;
            std::int32_t y = 0;
            if (ProjectToScreen(&point, &x, &y) == kProjectedOnScreen) {
                if (probe) {
                    // ProbeDepth clamps the pixel to the render target as below.
                    hidden = bw::render::gpu::ProbeDepth(static_cast<std::uint32_t>(i), x, y,
                                                         static_cast<float>(kSkyDepth) / 65535.0f)
                                 ? 1
                                 : 0;
                }
                else {
                    x = std::max(x, 0);
                    y = std::max(y, 0);
                    if (static_cast<std::uint32_t>(x) >= desc.dwWidth) {
                        x = static_cast<std::int32_t>(desc.dwWidth) - 1;
                    }
                    if (static_cast<std::uint32_t>(y) >= desc.dwHeight) {
                        y = static_cast<std::int32_t>(desc.dwHeight) - 1;
                    }
                    const auto* depth = static_cast<const std::uint16_t*>(desc.lpSurface);
                    if (depth[desc.lPitch * y / 2 + x] < kSkyDepth) {
                        hidden = 1;
                    }
                }
            }
        }
        hiddenCount += static_cast<float>(hidden != 0 ? 1 : 0);
    }
    if (depthTest != 0 && !probe) {
        // Unlocked even when the lock failed, as the original.
        static_cast<IDirectDrawSurface7*>(g_zBuffer)->Unlock(nullptr);
    }
    return (1.0f - hiddenCount * 0.2f) * 255.0f;
}

BW_HOOK_FUNCTION(0x007bed70, LH3DSky::SunVisibility);

}  // namespace lh3dlib
