# Black & White decompilation

A function-by-function C++20 decompilation of Lionhead's *Black & White* (2001), aimed at running the game on
current Windows with its long-standing problems fixed at the source.

The decompiled code runs **inside the original game process**: a launcher starts `runblack.exe`, loads
`BWDecomp.dll` before the game's first instruction, and every decompiled function replaces its original by a jump at
the original address. The game stays playable at every step while the share of original code shrinks. When nothing
original is left, the executable is only a loader and the code can be relinked as a standalone program.

This repository contains **no game code or data**. You need your own copy of the game.

| | |
|---|---|
| Original | `runblack.exe` 1.00 (English), linked 2001-03-09, MSVC 6.0 SP5, 12,722 functions, 963 RTTI classes |
| Decompiled | 24 functions (`CPUCheck`, `LHScreen` mode setting and presentation, `LHWin` window, quit and message handling, `LHMouse::SetPosition`, `LH3DRender::Start`, `FindGameDisc`); run `.\build.ps1` for the current figure |
| Fixes | SafeDisc, DirectX version check, 16-bit Direct3D crash, high-resolution crash, Direct3D 9 renderer for native high resolutions, windowed and borderless modes with scaling, hangs on quitting, disc requirement, video-memory overflow, audio memory, DPI scaling, DEP, CPU detection ([docs/FIXES.md](docs/FIXES.md)) |
| Status | Plays on Windows 10 and quits cleanly; see [CHANGELOG.md](CHANGELOG.md) |

## Supported executable

The decompilation patches code at fixed addresses, so it supports exactly one build:

| File | SHA-256 | |
|---|---|---|
| `crack\runblack.exe` on the disc | `86a30b08ab2ea8a240403d37f8f0303cf65aa5cde8e7f673049ab01654ccf755` | supported |
| `runblack.exe` installed by Setup | `b1163b44f4e6c5e64d81d5fcce95dea8efdc587ad52910df4d4942e01dbfb3d1` | same build wrapped in SafeDisc; needs `secdrv.sys`, which Windows no longer loads |

The launcher checks the hash and explains what to do if it finds a different file. Official patches 1.1/1.2 replace
`runblack.exe` with different builds and are not supported (see [docs/FIXES.md](docs/FIXES.md#official-patches)).

## Build

Needs Visual Studio 2022 (Desktop C++ workload) and CMake 3.25+. The output is 32-bit because it runs inside the
32-bit game.

```powershell
.\build.ps1            # configure, build, run unit tests, verify hooks against orig\runblack.exe
```

Output in `build\Release`: `BWLaunch.exe`, `BWDecomp.dll`, `bw_tests.exe`.

Hook verification (`tools/check_hooks.py`) needs a copy of the supported executable at `orig\runblack.exe` and a
Python 3.10+ with `capstone==5.0.6` and `pefile==2024.8.26` (path in `BW_PYTHON`).

## Install and play

`tools/install/install_game.py` installs the game from your disc image without the 2001 InstallShield setup:
cabinets (MD5-verified), the DLLs the setup copies from the disc root, the disc's `Audio` folder, the supported
`runblack.exe`, and the `GameDir` registry value. With `--decomp` it also copies the decompilation.

```powershell
python -I tools\install\install_game.py --iso BNW.iso --dest "G:\Games\Black & White" --decomp build\Release
```

Then start `BWLaunch.exe` in the install folder. Problems are logged to `BWDecomp.log` next to it; `BWDecomp.ini`
switches fixes and diagnostics on or off. No disc and no administrator rights are needed.

Settings are the game's own, in `HKEY_CURRENT_USER\Software\Lionhead Studios Ltd\Black & White\BWSetup` (`ScreenW`,
`ScreenH`, `ScreenD`, `FullScreen`, `detailidx`); without them the game uses 800×600. Widths up to 2048 render
natively (e.g. 1920×1080, 1920×1440); wider modes fall back to the largest one that works.

`[Display] WindowMode` in `BWDecomp.ini` chooses exclusive full screen (0, the original), a resizable window (1) or a
borderless window covering the screen (2). In modes 1 and 2 the picture is scaled to the window, so on a display
wider than 2048 pixels mode 2 fills the screen from a 1920×1080 render without changing the display mode, and
Alt+Tab is instant.

`[Display] Renderer=1` (experimental) replaces DirectDraw/Direct3D 7 with a Direct3D 9 renderer: native resolutions
above 2048 pixels (e.g. 2560×1440), no display-mode switch, vsync and anisotropic filtering options. See
[docs/RENDERER.md](docs/RENDERER.md).

Print Screen cannot capture the full-screen game. Set `ScreenshotKey` in `BWDecomp.ini` (e.g. `123` for F12) to
save PNG screenshots to `Screenshots\` in the game folder; `tools/screenshot.py` does the same from outside.

## Layout

| Path | Contents |
|---|---|
| `src/core` | Hooking, typed access to original functions/globals/imports, game heap, logging, configuration |
| `src/game/black` | Decompiled game code, one file per original translation unit (`C:\dev\black\*.cpp`) |
| `src/game/lhlib` | Decompiled Lionhead library code (`C:\DEV\Libs\LIONHEAD\lhlib\VER5.0\*.cpp`) |
| `src/fixes` | Compatibility fixes outside decompiled functions (process settings, import patches) |
| `src/render` | Direct3D 9 renderer: DirectDraw/Direct3D 7 implemented on Direct3D 9Ex |
| `src/diagnostics` | Run-time tracing without a debugger: exits, message boxes, exceptions, thread stacks, game debug output, screenshots |
| `src/dll`, `src/launcher` | `BWDecomp.dll` entry point and `BWLaunch.exe` |
| `symbols/functions.csv` | Every original function: address, end, name, class, original source file, status |
| `tools/` | Analysis and verification scripts (Python, static analysis only) |
| `tests/` | Unit tests and the hook-manifest writer |

Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how replacement works, [docs/RENDERER.md](docs/RENDERER.md) for the
renderer plan and [docs/WORKFLOW.md](docs/WORKFLOW.md)
before decompiling a function.
