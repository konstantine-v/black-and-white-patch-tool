#pragma once

// Lionhead library Windows layer (C:\DEV\Libs\LIONHEAD\lhlib\VER5.0\LHWin.cpp): WinMain, the game window, its window
// procedure and message pump. One global instance at 0x00d5c0f0; its LHScreen member at +0x10 is lhlib::g_screen.

#include "core/Layout.h"
#include "core/Memory.h"
#include "core/Windows.h"

#include <cstddef>
#include <cstdint>

namespace lhlib {

/// Partially recovered: only the members before the embedded LHScreen are known.
class LHWin {
public:
    /// Asks the game to quit: sets the flag the game loop polls and, while the window exists, sends it SC_CLOSE (the
    /// window procedure answers SC_CLOSE by calling back here). Called for Alt+F4 / the taskbar's Close, at the end
    /// of WinMain and when no display mode can be set. Fix: starts the shutdown watchdog (ShutdownTimeoutSeconds).
    void RequestQuit() noexcept;  // 0x00746e60

    [[nodiscard]] bool QuitRequested() const noexcept { return m_quitRequested != 0; }

private:
    friend struct LHWinLayout;

    HINSTANCE m_instance;        // 0x00
    std::int32_t m_showCommand;  // 0x04  WinMain's nCmdShow
    std::byte m_unknown08[4];    // 0x08
    std::int32_t m_quitRequested;  // 0x0c  never cleared once set
};

struct LHWinLayout {
    BW_ASSERT_OFFSET(LHWin, m_instance, 0x00);
    BW_ASSERT_OFFSET(LHWin, m_showCommand, 0x04);
    BW_ASSERT_OFFSET(LHWin, m_quitRequested, 0x0c);
    BW_ASSERT_SIZE(LHWin, 0x10);
};

BW_GLOBAL(LHWin, g_win, 0x00d5c0f0);
/// Keyboard accelerators of the game window.
BW_GLOBAL(HACCEL, g_accelerators, 0x00d62f18);
/// Set by the window procedure on SC_RESTORE; the pump below then restores the surfaces.
BW_GLOBAL(std::uint8_t, g_restoreRequested, 0x00d62f2b);

/// Registers the game's window class ("LIONHEAD", icon "AppIcon", menu "AppMenu") with `procedure` and loads the
/// "AppAccel" accelerators. Returns 0, or 2 when the class cannot be registered. Fix (CloseRequestsQuit): the class
/// gets WindowProcedure, which forwards to `procedure`.
[[nodiscard]] std::int32_t RegisterWindowClass(HINSTANCE instance, WNDPROC procedure) noexcept;  // 0x00746fc0

/// The game window's procedure when CloseRequestsQuit is on: WM_CLOSE asks the game to quit, like Alt+F4, and
/// everything else goes to the original procedure (0x007474e0). Originally WM_CLOSE (sent by taskkill and other
/// tools) reached DefWindowProc, which destroyed the window while the game kept running without one.
LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

/// Dispatches the game window's pending messages and, while the screen is suspended (window minimised or
/// deactivated), keeps doing so until it is restored. Called by the frame flip whenever messages are waiting.
/// Fix (MinimisedWait): waits for messages instead of spinning a CPU core, and stops waiting when the game is asked
/// to quit or its window is gone; the original never left, so closing a minimised game hung it.
void PumpMessages() noexcept;  // 0x00746f00

}  // namespace lhlib
