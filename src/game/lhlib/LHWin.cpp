#include "game/lhlib/LHWin.h"

#include "core/Config.h"
#include "core/Hook.h"
#include "core/Log.h"
#include "fixes/Fixes.h"
#include "game/lhlib/LHScreen.h"

namespace lhlib {

namespace {

/// Longest wait for a message while suspended; the loop re-checks its exit conditions at least this often.
constexpr DWORD kSuspendedWaitMilliseconds = 250;

constexpr std::int32_t kRegisterClassFailed = 2;

/// The procedure WinMain passed to RegisterWindowClass (0x007474e0).
WNDPROC g_gameWindowProcedure = nullptr;

}  // namespace

std::int32_t RegisterWindowClass(HINSTANCE instance, WNDPROC procedure) noexcept
{
    g_gameWindowProcedure = procedure;
    WNDCLASSA windowClass{};
    windowClass.style = CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS;
    windowClass.lpfnWndProc = bw::GetConfig().closeRequestsQuit ? &WindowProcedure : procedure;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconA(instance, "AppIcon");
    windowClass.lpszMenuName = "AppMenu";
    windowClass.lpszClassName = "LIONHEAD";
    if (RegisterClassA(&windowClass) == 0) {
        return kRegisterClassFailed;
    }
    g_accelerators = LoadAcceleratorsA(instance, "AppAccel");
    return 0;
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_CLOSE) {
        bw::log::Info("LHWin: WM_CLOSE received; quitting");
        g_win.RequestQuit();
        return 0;
    }
    return g_gameWindowProcedure(window, message, wParam, lParam);
}

void LHWin::RequestQuit() noexcept
{
    if (m_quitRequested != 0) {
        return;
    }
    m_quitRequested = 1;
    bw::log::Info("LHWin: quit requested");
    if (const std::uint32_t timeout = bw::GetConfig().shutdownTimeoutSeconds; timeout != 0) {
        bw::fixes::StartShutdownWatchdog(timeout);
    }

    const HWND window = g_screen.Window();
    if (IsWindow(window)) {
        SendMessageA(window, WM_SYSCOMMAND, SC_CLOSE, 0);
    }
}

void PumpMessages() noexcept
{
    const bool minimisedWait = bw::GetConfig().minimisedWait;
    do {
        MSG message;
        while (PeekMessageA(&message, g_mainWindow, 0, 0, PM_NOREMOVE)) {
            GetMessageA(&message, g_mainWindow, 0, 0);
            if (!TranslateAcceleratorA(g_mainWindow, g_accelerators, &message)) {
                TranslateMessage(&message);
                DispatchMessageA(&message);
            }
        }
        if (g_restoreRequested != 0) {
            g_restoreRequested = 0;
            g_screen.RestoreSurfaces();
        }

        if (!minimisedWait || g_screenSuspended == 0) {
            continue;
        }
        // Original: spun on PeekMessage until SC_RESTORE arrived. A quit request (taskbar Close on the minimised
        // game) is only acted on by the game loop, which never ran again, and a destroyed window never restores.
        if (g_win.QuitRequested()) {
            bw::log::Info("LHWin: leaving the suspended screen to quit");
            return;
        }
        if (!IsWindow(g_mainWindow)) {
            bw::log::Warning("LHWin: the game window was destroyed while suspended; quitting");
            g_win.RequestQuit();
            return;
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, kSuspendedWaitMilliseconds, QS_ALLINPUT);
    } while (g_screenSuspended != 0);
}

BW_HOOK_METHOD(0x00746e60, LHWin, RequestQuit);
BW_HOOK_FUNCTION(0x00746fc0, RegisterWindowClass);
BW_HOOK_FUNCTION(0x00746f00, PumpMessages);

}  // namespace lhlib
