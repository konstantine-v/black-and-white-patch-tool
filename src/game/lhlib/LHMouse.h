#pragma once

// Lionhead library mouse input (C:\DEV\Libs\LIONHEAD\lhlib\VER5.0\LHMouse.cpp). One instance at 0x00d5c2b4, a member
// of LHWin.

#include "core/Memory.h"
#include "core/Windows.h"

#include <cstddef>
#include <cstdint>

namespace lhlib {

/// Partially recovered.
class LHMouse {
public:
    /// Moves the cursor to `position` in game coordinates (the camera code keeps the hand in place this way). While
    /// the mouse is handled as relative movement the position is queued as input instead. Always returns 0.
    /// Fix (window modes): the position is mapped through the window's scaling before it becomes a screen position.
    std::int32_t SetPosition(const POINT* position) noexcept;  // 0x0074c0f0

private:
    std::byte m_unknown00[0x14];
    std::int32_t m_positionQueued;  // 0x14  set when SetPosition queued the position
};

BW_GLOBAL(LHMouse, g_mouse, 0x00d5c2b4);
/// Set while the mouse is read as relative movement; SetPosition then queues the position instead of moving the
/// cursor.
BW_GLOBAL(std::uint8_t, g_mouseRelative, 0x00d62f2a);

}  // namespace lhlib
