# Black & White Patch

A community patch that makes Lionhead's *Black & White* (2001) run properly on current Windows, and a step-by-step
rebuild of the game's own code in modern C++.

The original game was written for Windows 98 and DirectX 7, wrapped in a copy protection that Windows no longer
allows, and it never expected a screen wider than 2048 pixels. This patch fixes those problems at their source: instead
of working around the game from outside, it replaces the faulty code inside the running game with corrected C++.

> This repository contains **no game code or data**. You need your own copy of *Black & White*.

## What you get

- **It starts and it quits.** No SafeDisc driver, no disc in the drive, no "You need DirectX 7 or greater", no crash
  in 16-bit colour, and no hang when you close a minimised game.
- **Any resolution.** A Direct3D 9 renderer replaces DirectDraw and Direct3D 7, so 2560x1440 and beyond render
  natively, with no display-mode switch.
- **Windowed and borderless modes**, scaled to the window, with instant Alt+Tab.
- **A better picture.** Anti-aliasing (2 to 16 samples), anisotropic filtering, vsync, and textures that keep the
  colours the artists made instead of being squeezed to 16 bits.
- **Texture packs.** Dump any texture the game draws, repaint it at any size in 32-bit colour, and drop it into a
  folder. See [docs/TEXTURES.md](docs/TEXTURES.md).
- **Memory and DPI fixes** for machines with far more RAM and video memory than the game expected, and for
  high-DPI displays.
- **Diagnostics.** Problems go to a log file; a crash report shows where in the game's code it happened, and a stuck
  shutdown ends itself after a minute and leaves the evidence.

Every fix is a switch in `BWDecomp.ini`, and [docs/FIXES.md](docs/FIXES.md) explains each one: the symptom, the cause in
the original code, and what the patch does.

## Install

1. Install *Black & White* (a copy from the disc or a digital release works).
2. Get the unprotected `runblack.exe` (see [Supported game version](#supported-game-version)).
3. Run `BWDecomp-<version>-Setup.exe` from the [releases](https://github.com/konstantine-v/black-and-white-patch-tool/releases)
   and choose the game folder. Setup adds a launcher, the patch, a settings file and shortcuts; it changes none of the
   game's files, never overwrites your settings, and removes cleanly from Settings > Apps.
4. Start the game with the **Black & White (Patch)** shortcut, or `BWLaunch.exe` in the game folder.

No administrator rights are needed unless the game is below Program Files. Setup also runs without windows:
`BWDecompSetup.exe /quiet /dir "<game folder>"`. To install by hand, copy `BWLaunch.exe`, `BWDecomp.dll` and
`BWDecomp.ini` from the zip into the game folder.

The game's own options (resolution, detail level) stay where the game keeps them, in
`HKEY_CURRENT_USER\Software\Lionhead Studios Ltd\Black & White\BWSetup`. Without them the game uses 800x600.

### Settings

`BWDecomp.ini` in the game folder explains every setting. The ones most players change:

| Setting | Meaning |
|---|---|
| `[Display] WindowMode` | `0` exclusive full screen, `1` resizable window, `2` borderless window covering the screen |
| `[Display] AntiAliasing` | `4` or `8` for smoother edges, `0` = off |
| `[Display] Anisotropy` | `16` for sharper ground in the distance, `0` = off |
| `[Display] VSync` | `1` = no tearing |
| `[Display] Renderer` | `1` Direct3D 9 (default), `0` the original renderer, which cannot go beyond 2048 pixels wide |
| `[Diagnostics] ScreenshotKey` | e.g. `123` for F12: saves PNG screenshots to `Screenshots\` |
| `[Textures] Dump`, `Replace` | texture pack tools, see [docs/TEXTURES.md](docs/TEXTURES.md) |

If something goes wrong, `BWDecomp.log` next to the game says what happened.

## Supported game version

The patch changes code at fixed addresses, so it supports exactly one build of the game:

| File | SHA-256 | |
|---|---|---|
| `crack\runblack.exe` on the disc | `86a30b08ab2ea8a240403d37f8f0303cf65aa5cde8e7f673049ab01654ccf755` | supported |
| `runblack.exe` installed by the retail setup | `b1163b44f4e6c5e64d81d5fcce95dea8efdc587ad52910df4d4942e01dbfb3d1` | the same build wrapped in SafeDisc; needs `secdrv.sys`, which Windows no longer loads |

Both are version 1.00 (English). The launcher checks the file's hash and explains what to do if it finds something
else. Copy the disc's unprotected `runblack.exe` over the installed one. Lionhead's official patches 1.1 and 1.2
replace `runblack.exe` with different builds and are not supported yet (see
[docs/FIXES.md](docs/FIXES.md#official-patches)).

## How it works

A launcher (`BWLaunch.exe`) starts `runblack.exe` suspended, loads `BWDecomp.dll` into it before the game's first
instruction, and lets it run. The DLL checks that it is looking at the supported build, applies the fixes, and
replaces original functions with C++ rebuilt from them: each one is overwritten by a jump to its replacement, at the
original address.

The goal goes beyond fixing bugs. Every function that is rebuilt and verified shrinks the share of original code; the
game stays playable at every step. When nothing original is left, the executable is only a loader and the code can be
built as a standalone program.

| | |
|---|---|
| Original | `runblack.exe` 1.00 (English), linked 2001-03-09, MSVC 6.0 SP5, 12,722 functions, 963 RTTI classes |
| Rebuilt so far | 28 functions, each verified against the original executable by `tools/check_hooks.py`; run `.\build.ps1` for the current figure |
| Rebuilt areas | CPU detection, display mode setting and presentation, window, quit and message handling, mouse, 3D start-up, the sun's visibility test, texture loading, finding the game disc |
| Other code | A Direct3D 9 implementation of the DirectDraw and Direct3D 7 interfaces the game calls (`src/render`), the texture pack system, diagnostics and the fixes |
| Status | Plays on Windows 10 and quits cleanly; see [CHANGELOG.md](CHANGELOG.md) |

[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) describes replacement in detail, [docs/RENDERER.md](docs/RENDERER.md) the
renderer, and [docs/WORKFLOW.md](docs/WORKFLOW.md) how a function goes from the original to verified C++.

## Build

Needs Visual Studio 2022 (Desktop C++ workload) and CMake 3.25 or later. The output is 32-bit because it runs inside
the 32-bit game.

```powershell
.\build.ps1      # configure, build, run the unit tests, verify hooks against orig\runblack.exe
.\package.ps1    # the same, then write the release files to dist\
```

Output in `build\Release`: `BWLaunch.exe`, `BWDecomp.dll`, `BWDecompSetup.exe`, `bw_tests.exe`.

Hook verification needs a copy of the supported executable at `orig\runblack.exe` (never committed) and Python 3.10
or later with `capstone==5.0.6` and `pefile==2024.8.26`; set its path in `BW_PYTHON`. Without the executable the build
and the tests still run, and verification is skipped.

### Releases

`package.ps1` writes to `dist\`:

| File | Contents |
|---|---|
| `BWDecomp-<version>-Setup.exe` | The setup program (`src/setup`), which doubles as the uninstaller |
| `BWDecomp-<version>.zip` | The same files, for installing by hand |
| `BWDecomp-<version>-Release.zip` | Both, with the player readme |
| `BWDecomp-<version>-SHA256SUMS.txt` | Hashes of the three |

Nothing from the game goes in. Setup asks for administrator rights only when the game folder needs them, writes each
file next to its target and then moves it into place, and lists everything it installed, with hashes, in
`BWDecomp\Installed.txt`: uninstalling removes exactly that and keeps a `BWDecomp.ini` you changed. The programs are
not code-signed, so Windows SmartScreen warns when a downloaded setup is first started.

### Developers: installing the game from a disc image

`tools/install/install_game.py` installs the game from your disc image without the 2001 InstallShield setup:
cabinets (MD5-verified), the DLLs the setup copies from the disc root, the disc's `Audio` folder, the supported
`runblack.exe`, and the `GameDir` registry value. With `--decomp` it also copies the build output. It needs 7-Zip.

```powershell
python -I tools\install\install_game.py --iso BNW.iso --dest "G:\Games\Black & White" --decomp build\Release
```

## Layout

| Path | Contents |
|---|---|
| `src/core` | Hooking, typed access to original functions, globals and imports, the game heap, logging, configuration |
| `src/game/black` | Rebuilt game code, one file per original source file (`C:\dev\black\*.cpp`) |
| `src/game/lhlib`, `src/game/lh3dlib` | Rebuilt Lionhead library code: windowing, screen, mouse, 3D rendering, sky, textures |
| `src/fixes` | Compatibility fixes outside rebuilt functions: process settings, import patches |
| `src/render` | Direct3D 9 renderer: DirectDraw and Direct3D 7 implemented on Direct3D 9Ex, and the texture packs |
| `src/diagnostics` | Run-time tracing without a debugger: exits, message boxes, exceptions, thread stacks, screenshots |
| `src/dll`, `src/launcher`, `src/setup` | `BWDecomp.dll`, `BWLaunch.exe` and `BWDecompSetup.exe` |
| `symbols/functions.csv` | Every original function: address, end, name, class, original source file, status |
| `tools/` | Analysis and verification scripts (Python; static analysis, plus two that inspect a running game) |
| `tests/` | Unit tests and the hook-manifest writer |
| `docs/`, `release/` | Documentation; the player readme |

## Contributing

The most useful help right now: testing on different hardware and Windows versions (include `BWDecomp.log`), making
texture packs, and porting the differences in the official 1.1 and 1.2 patches. If you want to rebuild functions, read
[docs/WORKFLOW.md](docs/WORKFLOW.md) first. Every change keeps the game playable and the hook verification passing.

## Legal

*Black & White* is a game by Lionhead Studios. This project is not affiliated with or endorsed by Lionhead Studios or
the game's publishers. It contains none of the game's code or data and works only with a copy you own.

## Related projects

- [black-and-white-patch-launcher](https://github.com/konstantine-v/black-and-white-patch-launcher): the launcher
  project that goes with this patch.
