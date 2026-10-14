#include "game/lhlib/LHWin.h"

#include "core/Config.h"
#include "core/Hook.h"
#include "core/Log.h"
#include "fixes/Fixes.h"
#include "game/lhlib/LHScreen.h"

#include <windowsx.h>

namespace lhlib {

namespace {

/// Longest wait for a message while suspended; the loop re-checks its exit conditions at least this often.
constexpr DWORD kSuspendedWaitMilliseconds = 250;

constexpr std::int32_t kRegisterClassFailed = 2;
constexpr std::int32_t kMoveWindowFailed = 2;

/// The procedure WinMain passed to RegisterWindowClass (0x007474e0).
WNDPROC g_gameWindowProcedure = nullptr;

}  // namespace

std::int32_t RegisterWindowClass(HINSTANCE instance, WNDPROC procedure) noexcept
{
    g_gameWindowProcedure = procedure;
    WNDCLASSA windowClass{};
    windowClass.style = CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS;
    const bw::Config& config = bw::GetConfig();
    const bool wrap = config.closeRequestsQuit || config.windowMode != bw::WindowMode::FullScreen;
    windowClass.lpfnWndProc = wrap ? &WindowProcedure : procedure;
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
    if (message == WM_CLOSE && bw::GetConfig().closeRequestsQuit) {
        bw::log::Info("LHWin: WM_CLOSE received; quitting");
        g_win.RequestQuit();
        return 0;
    }
    if (IsClientMouseMessage(message) && g_screen.Scaled()) {
        const POINT point = g_screen.ClientToFrame({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        lParam = MAKELPARAM(point.x, point.y);
    }
    return g_gameWindowProcedure(window, message, wParam, lParam);
}

std::int32_t ResizeWindowToMode(std::uint16_t width, std::uint16_t height) noexcept
{
    if (!g_screen.Windowed()) {
        return 0;
    }
    const HWND window = g_screen.Window();
    const bw::WindowMode mode = bw::GetConfig().windowMode;
    if (mode == bw::WindowMode::Borderless) {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof monitor;
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
        const RECT& area = monitor.rcMonitor;
        SetWindowLongA(window, GWL_STYLE, static_cast<LONG>(WS_POPUP | WS_VISIBLE));
        SetWindowLongA(window, GWL_EXSTYLE, 0);
        if (!SetWindowPos(window, HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top,
                          SWP_FRAMECHANGED | SWP_SHOWWINDOW)) {
            return kMoveWindowFailed;
        }
        return 0;
    }

    ShowWindow(window, SW_SHOWNORMAL);
    RECT client{};
    GetClientRect(window, &client);
    RECT size{0, 0, g_screen.Width(), g_screen.Height()};
    if (width != 0 && width <= g_screen.Width()) {
        size.right = client.right - client.left;
    }
    if (height != 0 && height <= g_screen.Height()) {
        size.bottom = client.bottom - client.top;
    }
    const BOOL hasMenu = mode == bw::WindowMode::FullScreen ? TRUE : FALSE;
    AdjustWindowRect(&size, WS_OVERLAPPEDWINDOW, hasMenu);
    RECT current{};
    GetWindowRect(window, &current);
    if (!MoveWindow(window, current.left, current.top, size.right - size.left, size.bottom - size.top, TRUE)) {
        return kMoveWindowFailed;
    }
    return 0;
}

void ShowWindowMenu() noexcept
{
    SetMenu(g_mainWindow, bw::GetConfig().windowMode == bw::WindowMode::FullScreen ? g_windowMenu : nullptr);
}

void GetCursorFramePosition(HWND /*window*/, POINT* position) noexcept
{
    GetCursorPos(position);
    ScreenToClient(g_mainWindow, position);
    *position = g_screen.ClientToFrame(*position);
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
BW_HOOK_FUNCTION(0x00747ee0, ResizeWindowToMode);
BW_HOOK_FUNCTION(0x00746eb0, ShowWindowMenu);
BW_HOOK_FUNCTION(0x00747470, GetCursorFramePosition);

}  // namespace lhlib
