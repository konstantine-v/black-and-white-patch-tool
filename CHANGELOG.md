# Changelog

Versions follow [semantic versioning](https://semver.org/) while the decompilation is below 1.0: the minor version
increases with each wave of decompiled code or fixes, the patch version for corrections. The version is set in
`CMakeLists.txt` and tagged in git as `vX.Y.Z`.

## 0.10.1

Code review: safety, reuse and performance. No change in what the game does.

Security and memory safety:
- Replacement images: their size comes from the file and is now bounded (16384 per side) before buffer sizes are
  computed from it; a crafted image could overflow the pixel buffer in the 32-bit process.
- Paths outside the ANSI code page (e.g. a texture named in Chinese) no longer end the game: path text for messages and
  names is converted without exceptions (`log::PathText`).
- Texture dumps stay inside `Textures\Dump` whatever the source name contains.
- COM methods of the DirectDraw/Direct3D 7 replacement that allocate return `DDERR_OUTOFMEMORY` instead of letting a C++
  exception unwind into the game's code (`Guarded`); logging helpers there no longer throw.
- Direct3D 9 objects are held by typed `ComPtr`s instead of untyped pointers released by hand (a depth buffer could
  leak when allocation failed).
- The launcher quotes game arguments correctly (an embedded quote could split or add arguments), frees the injected
  path on every error, checks `CommandLineToArgvW` and `ResumeThread`, and owns its handles (`UniqueHandle`).
- PE headers are validated (DOS and PE signatures) before sections or imports are read.
- `TraceDirectX` stubs are made execute-only after they are written (no writable and executable memory) and no longer
  leak on allocation failure.
- The exception logger formats stack addresses into a stack buffer: no heap use while the faulting thread may hold the
  heap lock.
- `GameAllocated::operator new` is `noexcept`, so a failed game-heap allocation yields nullptr as intended.

Reuse:
- One `ModulePath` helper (no `MAX_PATH` truncation) replaces five copies.
- One image module (`core/Bitmap`, Windows Imaging Component) for screenshots and texture packs; the hand-written PNG
  encoder and the old `BW_DUMP_TEXTURES` aid are gone.

Performance:
- Screenshots are compressed PNGs (4 MB instead of 11 MB at 2560x1440).
- Blit and surface logging costs nothing without `[Log] Debug=1` (it formatted a string on every blit).
- One `WriteFile` per log line instead of five; the launcher reads the executable in one go to hash it; the
  `TraceDirectX` report looks up method addresses in a sorted table.

Tests: argument quoting, handle ownership, module paths, PE header checks, UTF-8 path text, PNG round trip (44 tests).

## 0.10.0

Texture packs by name, and full-colour file textures.

- Decompiled the texture loaders `LoadRawTexture` (`0x0078cde0`), `LH3DTexture::LoadSegment` (`0x0078ded0`) and
  `LoadCompressedTexture` (`0x007cc750`): they register what they load with the renderer.
- Dumps and replacements by source name: `Textures\Replace\data\textures\sky.png`, `Textures\Replace\segments\2b.png`;
  hash names still work for everything else.
- `Data\Textures\*.raw` (24-bit with alpha files) are drawn at full colour instead of 16-bit (`FullPrecisionTextures`).
- Textures redrawn every frame (object shadows) are no longer hashed or dumped.
- Tested in a skirmish game with named 4x replacements for the sea and the village banners.

## 0.9.0

Texture packs: replacement textures of any resolution in 32-bit colour.

- `[Textures] Dump=1` saves every distinct texture the game draws to `Textures\Dump\` as PNG, named by a hash of its
  contents; `Replace=1` (default) draws images from `Textures\Replace\` named after those hashes instead: any size,
  8 bits per channel with alpha, PNG/DDS/BMP/JPEG/TIFF, with generated mipmaps. See `docs/TEXTURES.md`.
- Whole texture pages are replaced; fonts and the landscape are built at runtime and cannot be replaced yet.
- Tested: the creature skin page replaced with a 4x larger image in the front end.

## 0.8.0

The Direct3D 9 renderer becomes the default, with anti-aliasing and better textures.

- `[Display] Renderer` defaults to 1 (Direct3D 9); without Direct3D 9Ex the original renderer runs. `Renderer=0`
  restores it.
- `AntiAliasing` (e.g. 4): multisampled rendering, resolved for presentation, screenshots and CPU access.
- `FullPrecisionTextures=1`: DXT textures the game unpacks into 16-bit textures keep their decoded colours (A8R8G8B8
  on the GPU) instead of 4 bits per channel.
- `LH3DSky::SunVisibility` (`0x007bed70`) decompiled: the sun glare's visibility test. With the Direct3D 9 renderer it
  uses occlusion queries instead of locking the z-buffer every frame, so it never waits for the GPU.
- Duplicate `BeginScene`/`EndScene` calls of the game are dropped instead of failing in Direct3D 9.
- Tested at 2560x1440 with 4x anti-aliasing: front end and a skirmish game at 143 fps (display refresh), sun probes
  answered every frame.

## 0.7.0

A Direct3D 9 renderer: the game at native resolutions above 2048 pixels.

- `[Display] Renderer=1` (experimental, off by default): DirectDraw and Direct3D 7 implemented on Direct3D 9Ex
  (`src/render`). Tested with the intro videos, the front end and a skirmish game, at 1920x1080 and 2560x1440, with
  quitting, minimising and restoring. `VSync` and `Anisotropy` settings.
- Compressed (DXT) surfaces: decoding and encoding in blits, sizes reported as DirectDraw does.
- Depth reads (the sun-visibility test) through a lazily filled view: only touched pages are read back.
- `LHScreen::Flip` skips the per-frame back-buffer lock with the new renderer (the software rasteriser is dead code).
- Debug aids: frame rate and per-frame statistics, surface/blit kinds, texture dumps (`BW_DUMP_TEXTURES`).

## 0.6.0

Groundwork for a Direct3D 9 renderer: what the game asks of DirectDraw and Direct3D 7.

- `[Diagnostics] TraceDirectX`: counts every DirectDraw/Direct3D 7 method call per calling function and writes
  `BWDecomp.directx.csv`; `tools/directx_report.py` summarises it. Handles Direct3D 7's per-device method tables and
  its rewriting of the draw entries, and reports method pointers the game copies.
- `docs/RENDERER.md`: the measured API usage and the design of the replacement renderer.
- `tools/dx_methods.py`: interface method tables generated from the Windows SDK headers.

## 0.5.0

Windowed and borderless play, scaled to any window size.

- `[Display] WindowMode`: 0 = exclusive full screen (original), 1 = resizable window, 2 = borderless window covering
  the monitor; `KeepAspectRatio` letterboxes instead of stretching. A 2560×1440 screen is filled from a 1920×1080
  render without a display-mode change.
- Decompiled `LHScreen::SetFullScreen`, `ResizeWindowToMode`, `ShowWindowMenu`, the mouse thread's cursor read and
  `LHMouse::SetPosition` (`LHMouse.cpp`); 24 functions in total. Mouse input is mapped through the scaling.
- The debug menu bar of the original windowed mode is hidden; assertion dialogs stay suppressed as in full screen.
- `BW_IMPORTED_DATA` for variables other DLLs export to the game.

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
