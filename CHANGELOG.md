# Changelog

Versions follow [semantic versioning](https://semver.org/) while the decompilation is below 1.0: the minor version
increases with each wave of decompiled code or fixes, the patch version for corrections. The version is set in
`CMakeLists.txt` and tagged in git as `vX.Y.Z`.

## 0.4.0

Quitting always works.

- Decompiled `LHWin::RequestQuit`, `RegisterWindowClass` and the message pump (`LHWin.cpp`), and `LHScreen::Flip`;
  19 functions in total.
- Fixed: closing the game while it was minimised or after Alt+Tab hung it with a CPU core at 100 %; it now quits, and
  idles while minimised (`MinimisedWait`).
- Fixed: `WM_CLOSE` (e.g. `taskkill`) destroyed the window and left the game running; it now quits like Alt+F4
  (`CloseRequestsQuit`).
- Shutdown watchdog: a quit that has not finished after `ShutdownTimeoutSeconds` (60) logs every thread's stack and
  ends the process.
- Screenshots of the full-screen game as PNG (`ScreenshotKey`, `tools/screenshot.py`).
- `tools/threads.py`: registers and return addresses of every thread of a running (hung) game.

## 0.3.0

High resolutions no longer crash the game.

- Decompiled `LH3DRender::Start` (`LH3DLIB`): reports and unwinds a failed Direct3D start-up instead of crashing on the
  null device.
- `LHScreen::SetMode` falls back to smaller display modes, same aspect ratio first, when Direct3D refuses a mode
  (`DisplayModeFallback`); 2560×1440 now runs at 1920×1080. Root cause: Direct3D 7 rejects render targets wider than
  2048 pixels (`DDERR_INVALIDOBJECT`).
- `FastcallFunction` wrapper for original `__fastcall` functions.

## 0.2.0

First version that starts on Windows 10 and reaches the front end.

- Decompiled `LHScreen::SetMode` and `FindGameDisc` (`PCMain.cpp`); 14 functions in total.
- Fixes: DirectX version check (`DirectXVersionReport`), 16-bit colour Direct3D crash (`ColorDepth32`), disc
  requirement (`DiscFromInstallFolder`).
- Diagnostics: exit, message-box and exception tracing with return addresses, game debug output in the log.
- `tools/install/install_game.py`: installs from the disc image without the original setup; writes `GameDir` (HKCU).
- Import patching by ordinal.
- Docs: settings and saves live under `HKEY_CURRENT_USER` (no administrator rights needed).

## 0.1.0

- Project structure: `BWLaunch.exe` starts the supported `runblack.exe` suspended and loads `BWDecomp.dll`, which
  verifies the image, applies fixes and installs hooks for decompiled functions.
- Typed hooking and original-code access (`core/Hook.h`, `core/Call.h`, `core/Memory.h`), game heap, logging,
  `BWDecomp.ini` configuration.
- Symbols: 12,722 functions with RTTI class owners and original source files (`symbols/functions.csv`).
- Decompiled: `CPUCheck` (11 functions), `LHScreen::GetAvailableVideoMemory`.
- Fixes: video-memory overflow, audio cache size, DPI awareness, executable `SELFMOD` section, CPU family detection.
- Tools: PE inventory, RTTI recovery, symbol database, hook verification, disassembly and cross-reference helpers.
