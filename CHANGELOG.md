# Changelog

Versions follow [semantic versioning](https://semver.org/) while the decompilation is below 1.0: the minor version
increases with each wave of decompiled code or fixes, the patch version for corrections. The version is set in
`CMakeLists.txt` and tagged in git as `vX.Y.Z`.

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
