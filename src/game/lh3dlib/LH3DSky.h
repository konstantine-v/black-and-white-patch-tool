#pragma once

// The sky: sun, moon and weather (Data\WeatherSystem). Next to LH3D's renderer in the image; the original source file
// is not named in the binary. Partially decompiled; unknown globals keep their address in the name.

#include "core/Memory.h"

#include <cstdint>

namespace lh3dlib {

struct Vector3 {
    float x;
    float y;
    float z;
};

/// The camera; only its position (first member) is known.
struct Camera {
    Vector3 position;
};

struct LH3DSky {
    /// How visible the sun is for its glare, 0 (hidden) to 255: five points around the sun, each hidden by the
    /// landscape or, with a 16-bit z-buffer, by anything drawn in front of it (its depth is nearer than the sky's).
    /// The caller (0x007bebd0) smooths the result over frames.
    ///
    /// The original locked the whole z-buffer every frame to read five values, which stalls the CPU until the GPU has
    /// finished the frame. With the Direct3D 9 renderer the depth test is an occlusion query instead (its answer lags
    /// a frame or two, which the smoothing hides).
    [[nodiscard]] static float SunVisibility() noexcept;  // 0x007bed70
};

/// Sun position in the world.
BW_GLOBAL(Vector3, g_sunPosition, 0x00ec1570);
BW_GLOBAL(Camera, g_camera, 0x00d78bd8);
/// Half the spacing of the five test points around the sun (500).
BW_GLOBAL(float, g_sunTestSpread, 0x00b5b5f8);
/// The projection's near distance; the test points are moved this far beyond the sun, away from the camera.
BW_GLOBAL(float, g_nearDistance, 0x00dc8c90);
/// The depth test runs only when this is 3, 4 or 6 (set at start-up; 4 on current hardware).
BW_GLOBAL(std::int32_t, g_unknown00b5a2d4, 0x00b5a2d4);

}  // namespace lh3dlib
