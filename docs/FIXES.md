# Fixes

What stops *Black & White* 1.00 from running properly on current Windows, where in the original the cause lies, and
what the decompilation does about it. Addresses refer to the supported `runblack.exe`. Settings are in
`BWDecomp.ini`.

## Fixed

### SafeDisc

**Symptom:** the installed game does not start on Windows Vista or later.
**Cause:** the retail `runblack.exe` is wrapped in SafeDisc, which needs the `secdrv.sys` kernel driver; Windows
blocks it since 2015.
**Fix:** the decompilation targets the unprotected build of the same version from the disc's `crack\` folder
(identical size, entry point restored to `0x00737b8e`). `BWLaunch.exe` recognises the SafeDisc build by its hash and
says what to do. The unprotected build still carries SafeDisc's dead `stxt774`/`stxt371` sections; they disappear
when the decompilation becomes a standalone executable.

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
**Cause:** the software rasteriser rewrites immediates in its own code for speed (`0x00ee0285` stores the surface pitch
into four instructions; called from `0x00748bcb` and `0x00749750`). That code lives in the `SELFMOD` section, which is
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

## Not fixed yet

| Problem | What is known | Next step |
|---|---|---|
| Crash above 1080p / at high resolutions | Mode setting is `LHScreen::SetMode` (`0x00748d70` → `0x00748de0`); width/height are 16-bit there, so the limit is elsewhere (render buffers, interface layout or setup's mode list). | Reproduce under the launcher and decompile the faulting function. |
| Windowed mode | `0x00747050` creates an 800×600 `WS_OVERLAPPEDWINDOW`; `SetMode` switches styles. | Decompile `LHWin.cpp`/`LHScreen.cpp` window handling. |
| Settings need administrator rights | Settings live in `HKLM\...\Black & White\BWSetup` (read by `PCMain` at `0x005f2650`) through `LHLogR.dll`'s `Registry*` functions; non-elevated writes land in the registry VirtualStore. | Redirect to `HKCU` (or `BWDecomp.ini`) once `PCMain` is decompiled. |
| Music requires the disc | Music banks are read from `audio/music/...` relative paths; the installer does not copy `Audio\`. | Copying the folder works today; decompile the bank lookup to search the install folder. |

## Official patches

Lionhead's 1.1 and 1.2 patches replace `runblack.exe` with later builds (1.1 notes include a creature-leash lock-up
and SafeDisc start-up problems on Windows 2000). Their code fixes are not in this build. Including them means
comparing the patched executables with this one function by function and porting the differences into the
decompiled code; that needs the patch files, which are not part of this repository.
