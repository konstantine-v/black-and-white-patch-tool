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
| Decompiled | 12 functions (`CPUCheck`, `LHScreen::GetAvailableVideoMemory`); run `.\build.ps1` for the current figure |
| Fixes | SafeDisc, video-memory overflow, audio memory, DPI scaling, DEP, CPU detection ([docs/FIXES.md](docs/FIXES.md)) |

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

1. Install the game from the disc (Setup.exe) and copy the disc's `Audio` folder into the game folder if the installer
   did not.
2. Replace the installed `runblack.exe` with the disc's `crack\runblack.exe` (the supported, unprotected build).
3. Copy `BWLaunch.exe`, `BWDecomp.dll` and `BWDecomp.ini` into the game folder.
4. Start the game with `BWLaunch.exe`. Problems are logged to `BWDecomp.log` in the same folder.

Resolution and detail level are still read from the game's registry key
`HKLM\SOFTWARE\WOW6432Node\Lionhead Studios Ltd\Black & White\BWSetup` (`ScreenW`, `ScreenH`, `ScreenD`,
`FullScreen`, `detailidx`), as written by the game's setup tool.

## Layout

| Path | Contents |
|---|---|
| `src/core` | Hooking, typed access to original functions/globals/imports, game heap, logging, configuration |
| `src/game/black` | Decompiled game code, one file per original translation unit (`C:\dev\black\*.cpp`) |
| `src/game/lhlib` | Decompiled Lionhead library code (`C:\DEV\Libs\LIONHEAD\lhlib\VER5.0\*.cpp`) |
| `src/fixes` | Compatibility fixes outside decompiled functions (process settings, import patches) |
| `src/dll`, `src/launcher` | `BWDecomp.dll` entry point and `BWLaunch.exe` |
| `symbols/functions.csv` | Every original function: address, end, name, class, original source file, status |
| `tools/` | Analysis and verification scripts (Python, static analysis only) |
| `tests/` | Unit tests and the hook-manifest writer |

Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how replacement works and [docs/WORKFLOW.md](docs/WORKFLOW.md)
before decompiling a function.
