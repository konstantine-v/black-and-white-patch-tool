#include "game/lhlib/LHMouse.h"

#include "core/Call.h"
#include "core/Hook.h"
#include "game/lhlib/LHScreen.h"

namespace lhlib {

namespace {

/// Message the window procedure turns into a cursor-position input event.
constexpr UINT kQueuedPositionMessage = 0x8007;

// Not decompiled yet; both act on g_mouse.
inline constexpr bw::ThiscallFunction<0x0074bc90, void(LHMouse*, std::int32_t, std::int32_t)> QueuePosition{};
inline constexpr bw::ThiscallFunction<0x0074c9a0, void(LHMouse*, std::int32_t, std::int32_t)> QueueEvent{};

}  // namespace

std::int32_t LHMouse::SetPosition(const POINT* position) noexcept
{
    if (g_mouseRelative != 0) {
        m_positionQueued = 1;
        PostMessageA(g_screen.Window(), kQueuedPositionMessage, 0, 0);
        QueuePosition(&g_mouse, position->x, position->y);
        QueueEvent(&g_mouse, 1, 4);
        return 0;
    }
    POINT screen = g_screen.FrameToClient(*position);
    ClientToScreen(g_screen.Window(), &screen);
    SetCursorPos(screen.x, screen.y);
    return 0;
}

BW_HOOK_METHOD(0x0074c0f0, LHMouse, SetPosition);

}  // namespace lhlib
