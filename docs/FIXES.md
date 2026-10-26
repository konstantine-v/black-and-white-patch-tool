# Fixes

What stops *Black & White* 1.00 from running properly on current Windows, where in the original the cause lies, and
what the patch does about it. Addresses refer to the supported `runblack.exe`. Settings are in
`BWDecomp.ini`.

## Fixed

### SafeDisc

**Symptom:** the installed game does not start on Windows Vista or later.
**Cause:** the retail `runblack.exe` is wrapped in SafeDisc, which needs the `secdrv.sys` kernel driver; Windows
blocks it since 2015.
**Fix:** the patch targets the unprotected build of the same version from the disc's `crack\` folder
(identical size, entry point restored to `0x00737b8e`). `BWLaunch.exe` recognises the SafeDisc build by its hash and
says what to do. The unprotected build still carries SafeDisc's dead `stxt774`/`stxt371` sections; they disappear
when the decompilation becomes a standalone executable.

### "You need DirectX 7 or greater to play Black & White."

**Symptom:** the game shows this message and quits immediately, on any current Windows.
**Cause:** `GameMain` (`0x005f1550`) asks the disc's 1999 `DSETUP.dll` (`DirectXSetupGetVersion`, imported by ordinal
11) and requires a version above `0x00040006`. That DLL derives the version from system file versions and does not
recognise current Windows, although the registry correctly reports DirectX 4.09.00.0904.
**Fix:** the import is answered with DirectX 9.0c (`4.09.00.0904`), the version current Windows registers
(`DirectXVersionReport`).

### Crash at start-up in 16-bit colour

**Symptom:** access violation at `0x00775031` right after "ZBUFFER 16 bits" in the log; with the default settings the
game never gets past start-up.
**Cause:** the game defaults to 16-bit colour (`ScreenD`, read by `PCMain` at `0x005f2650`). Current drivers do not
create a hardware Direct3D 7 device, or attach a z-buffer, on a 16-bit display mode. `LH3DRender`'s Direct3D start-up
(`0x00785fd0`) ignores the failed `CreateDevice`, so the first transform call goes through a null device pointer
(`0x00de9494`).
**Fix:** `LHScreen::SetMode` (`0x00748d70`) is decompiled and runs 16-bit requests at 32 bits (`ColorDepth32`). The
game already supports 32-bit colour: textures, fonts and pixel formats follow the mode actually set.

### Crash at resolutions wider than 2048 pixels

**Symptom:** the game crashes at start-up above 1080p on 16:9 displays (2560×1440 and up); 1920×1440 works.
**Cause:** Direct3D 7 on current drivers rejects back buffers wider than 2048 pixels as render targets:
`IDirect3D7::CreateDevice` returns `DDERR_INVALIDOBJECT` (`0x88760082`). `LH3DRender::Start` (`0x00784520`) ignored
the failed start-up and crashed on the null device.
**Fix:** `LH3DRender::Start` is decompiled: it reports the `CreateDevice` error, releases the half-started renderer
and returns 0. `LHScreen::SetMode` then tries the display's smaller modes, same aspect ratio first
(`DisplayModeFallback`), so 2560×1440 runs at 1920×1080 instead of crashing. Rendering above 2048 pixels needs a
renderer that does not go through Direct3D 7; that is planned with the decompilation of LH3D.

### No windowed mode; resolutions above 2048 pixels

**Symptom:** the game always takes over the screen in exclusive full screen; on displays wider than 2048 pixels it
cannot use the native resolution (see above) and the monitor switches to a smaller mode.
**Cause:** `LHScreen` has a complete windowed path (Lionhead's development mode): `ApplyMode` (`0x00748de0`) keeps the
desktop mode, creates an off-screen 3D back buffer and a clipper, and `Flip` blits the frame into the window. But
`PCMain` calls `LHScreen::SetFullScreen(1)` (`0x007484a0`) unconditionally at start-up (`0x005f291b`), whatever the
`FullScreen` setting says, so the windowed path is never used. It also blits 1:1, with a debug menu bar.
**Fix:** `WindowMode` in `[Display]`: 1 = a resizable window, 2 = a borderless window covering the monitor.
`SetFullScreen` is decompiled and keeps the windowed path; `LHScreen::Flip` scales the frame to the window (keeping the
aspect ratio, `KeepAspectRatio`) and clears the bars; `ResizeWindowToMode` (`0x00747ee0`) sizes the window or makes it
cover the monitor; `ShowWindowMenu` (`0x00746eb0`) leaves the debug menu hidden. Mouse positions are mapped back to
game coordinates in the window procedure, the mouse thread (`0x00747470`) and when the game moves the cursor
(`LHMouse::SetPosition`, `0x0074c0f0`). In mode 2 a 2560×1440 screen shows the game rendered at 1920×1080 (the largest
16:9 mode Direct3D 7 accepts), scaled to fill it, without changing the display mode.

### DirectDraw and Direct3D 7 on current Windows

**Symptom:** at most 2048 pixels wide, no anti-aliasing, banding in textures, a GPU stall every frame.
**Cause:** Direct3D 7 refuses wider render targets; the game unpacks DXT textures into 4-bit-per-channel textures; the
sun glare's visibility test (`LH3DSky::SunVisibility`, `0x007bed70`) locks the z-buffer every frame, which waits for
the GPU to finish the frame.
**Fix:** the Direct3D 9 renderer ([RENDERER.md](RENDERER.md)), the default since 0.8.0 (`Renderer=0` for the
original): any resolution, `AntiAliasing`, `FullPrecisionTextures`, and the sun test through occlusion queries
(`SunVisibility` is decompiled; with the original renderer it still locks the z-buffer, as the original did).

### Hang when closing a minimised game

**Symptom:** after Alt+Tab (or minimising), closing the game from the taskbar does nothing; the process keeps one CPU
core busy and has to be ended in Task Manager.
**Cause:** when the window is deactivated, `LHScreen` suspends its surfaces (`0x00749780` sets `0x00d63410`) and the
frame flip enters the message pump `0x00746f00`, which spins on `PeekMessage` until the window is restored
(`SC_RESTORE` → `0x007497a0`). Closing sends `SC_CLOSE`, which only sets the quit flag (`LHWin::RequestQuit`,
`0x00746e60`) that the game loop polls; the game loop never runs again, so the game never quits. A window destroyed
while suspended never restores either.
**Fix:** the pump is decompiled (`src/game/lhlib/LHWin.cpp`). While suspended it waits for messages
(`MsgWaitForMultipleObjects`) instead of spinning, and returns to the game loop once a quit is requested or the window
is gone (`MinimisedWait`).

### `WM_CLOSE` leaves the game running without a window

**Symptom:** closing the game with `taskkill` (without `/F`) or another tool that sends `WM_CLOSE` leaves a
windowless process running (or hung).
**Cause:** the window procedure (`0x007474e0`) handles Alt+F4 (`SC_CLOSE`) as a quit request but passes `WM_CLOSE` to
`DefWindowProc`, which destroys the window; nothing tells the game loop.
**Fix:** `RegisterWindowClass` (`0x00746fc0`) is decompiled and registers a wrapper procedure that turns `WM_CLOSE`
into the same quit request as Alt+F4 and passes every other message to the original (`CloseRequestsQuit`).

### Shutdown safety net

Not a bug in itself: if the game is still running `ShutdownTimeoutSeconds` (default 60) after it was asked to quit,
`BWDecomp` logs the stack of every thread to `BWDecomp.log` and ends the process, so a shutdown problem that is not
fixed yet never needs Task Manager and leaves the evidence for fixing it. A normal quit takes under a second.

### Disc required

**Symptom:** without the disc in a drive, the game sits on a black screen for 30 seconds and quits.
**Cause:** `FindGameDisc` (`0x005f1250`) looks on drives D: to Y: for a volume labelled `BNW` with file system `CDFS`;
`GameMain` retries once a second for 30 seconds. The drive letter (`0x00b4dee8`) is also where file opens fall back to
when a file is missing from the install folder. The unprotected executable still contains this check.
**Fix:** decompiled. A real disc or mounted image is still found on any drive; otherwise the install folder is
accepted (`DiscFromInstallFolder`). `tools/install/install_game.py` copies everything the game reads from the disc
(the `Audio` folder) into the install folder.

### Low-detail textures on cards with a lot of video memory

**Symptom:** blurry textures and landscape on any modern graphics card.
**Cause:** at start-up `PCMain` (`0x005f2903`) calls `LHScreen::GetAvailableVideoMemory` (`0x00749a70`) and executes
`cmp eax, 13000000 / jge` — a signed comparison — to decide whether to set the low-detail flag at `0x00df820c`.
The original function returns DirectDraw's free video memory as a signed `int`: above 2 GB it wraps negative. It also
returns an uninitialised value when `GetAvailableVidMem` fails, and leaks the `IDirectDraw2` interface it obtains.
**Fix:** decompiled (`src/game/lhlib/LHScreen.cpp`): the interface is released, failures return 0, and the result is
clamped to `MaxReportedVideoMemoryMB` (default 1024; 0 = only clamp to `INT32_MAX`).

### Audio cache sized from all physical memory

**Symptom:** out-of-memory failures in long sessions (32-bit process, 2 GB address space).
**Cause:** the `GAudio` constructor (`0x00420f80`, `Audio.cpp`) gives the audio system a sample cache of
`MEMORYSTATUS.dwTotalPhys / 8`. On the 128–256 MB machines of 2001 that was 16–32 MB; on a current PC the game sees
2–4 GB and reserves 256–512 MB for audio.
**Fix:** `GlobalMemoryStatus` (the image's only caller is `GAudio`) is patched to report at most
`MaxReportedPhysicalMemoryMB` (default 512, giving a 64 MB cache). To be replaced by the decompiled constructor.

### DPI scaling

**Symptom:** wrong window size or blurry, offset output on displays scaled above 100 %.
**Cause:** the game is not DPI-aware, so Windows virtualises coordinates. `LHScreen::SetMode` (`0x00748de0`) and the
window setup in `LHWin.cpp` (`0x00747050`) size the full-screen window with `GetSystemMetrics`, which then reports the
scaled desktop rather than the mode being set.
**Fix:** the process declares per-monitor DPI awareness (v2, falling back to system awareness) before the game creates
its first window (`DpiAware`).

### Self-modifying rasteriser code under DEP

**Symptom:** access violation on the first frame when DEP is enforced for all programs.
**Cause:** the software rasteriser rewrites immediates in its own code for speed (`0x00ee0285` stores the back buffer's
address into four instructions; called from `0x00748bcb` and `0x00749750`). That code lives in the `SELFMOD` section, which is
flagged as read/write *data* (`0xC0000040`), not executable. The image does not opt into DEP, so the default policy
spares it, but the "AlwaysOn"/"OptOut" policies do not.
**Fix:** `SELFMOD` is made `PAGE_EXECUTE_READWRITE` at start-up (`ExecutableSelfModifyingCode`).

### CPU detection

**Symptom:** modern AMD processors are reported as "an Early Pentium" (the "Family 15" problem).
**Cause:** `CPUCheck::IsEarlyPentium` (`0x0045c5e0`) tests `(cpuid(1).eax & 0x500) == 0x500` to mean "family 5"; it
is also true for families 7 and 15, and every AMD processor since the Athlon 64 reports base family 15.
In this build the class only feeds the first-run system report `Config<n>.txt` (`Config.cpp`, `0x00455f80`), so the
misdetection does not change gameplay.
**Fix:** `CPUCheck` is fully decompiled (`src/game/black/CPUCheck.cpp`): the family is decoded properly, models the
2001 table lacks get the CPUID brand string instead of an uninitialised buffer, unknown vendors get a defined value,
and the report's unbounded `sprintf`/`strcat` into 1,000/4,000/5,000-byte buffers are bounded.

## Settings and saves

Not a bug, but often reported as one: `LHLogR.dll`'s registry helpers default to `HKEY_CURRENT_USER` and the game
only ever selects that root, so settings (`Software\Lionhead Studios Ltd\Black & White\BWSetup`: `ScreenW`,
`ScreenH`, `ScreenD`, `FullScreen`, `detailidx`, ...) never need administrator rights. Profiles and saves go below the
`GameDir` value of `Software\Lionhead Studios Ltd\Black & White` (read at `0x00715a20`); without it the game uses
`\dev\black`, Lionhead's own development folder. `install_game.py` writes `GameDir`.

## Not fixed yet

| Problem | What is known | Next step |
|---|---|---|
| Rendering wider than 2048 pixels with the original renderer (`Renderer=0`) | Direct3D 7 refuses such render targets (see above), also in windowed mode. The Direct3D 9 renderer ([RENDERER.md](RENDERER.md)), the default since 0.8.0, renders at any resolution. | — |
| Videos in window modes | Bink videos draw straight into the primary surface at the window's position (`LHScreen` `+0x20`, `0x007498d0`), unscaled. | Decompile the video playback path. |

## Official patches

Lionhead's 1.1 and 1.2 patches replace `runblack.exe` with later builds (1.1 notes include a creature-leash lock-up
and SafeDisc start-up problems on Windows 2000). Their code fixes are not in this build. Including them means
comparing the patched executables with this one function by function and porting the differences into the
decompiled code; that needs the patch files, which are not part of this repository.
