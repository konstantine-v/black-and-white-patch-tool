# Architecture

## Incremental replacement

Rewriting 12,722 functions before anything runs would leave nothing to test for years. Instead the decompilation
lives in a DLL inside the original process and replaces functions one at a time; the original code keeps running
everything not yet decompiled. Every replacement can be switched off again (`[Hooks] Disabled=` in `BWDecomp.ini`),
which makes regressions easy to bisect.

```
BWLaunch.exe
  ├─ checks runblack.exe's SHA-256 (supported build, or explains the SafeDisc build)
  ├─ CreateProcess(runblack.exe, CREATE_SUSPENDED)        ← no game code has run yet
  ├─ CreateRemoteThread(LoadLibraryW "BWDecomp.dll")
  │     DllMain
  │       ├─ load BWDecomp.ini, open BWDecomp.log
  │       ├─ hash runblack.exe's .text in memory          ← refuse to patch anything else
  │       ├─ fixes::InstallAll                            ← process settings, import patches
  │       └─ HookRegistry::InstallAll                     ← jmp rel32 over every decompiled original
  └─ ResumeThread                                         ← the game starts, already patched
```

`runblack.exe` has relocations stripped and always loads at `0x00400000`, so original addresses are constants.

## Talking to original code

Decompiled code and original code share one address space and one set of objects, so they must agree on the binary
interface exactly. `src/core` makes every crossing typed:

| Need | Use | Example |
|---|---|---|
| Replace an original function | `BW_HOOK_FUNCTION`, `BW_HOOK_METHOD`, `BW_HOOK_CONSTRUCTOR`, `BW_HOOK_DESTRUCTOR`, `BW_HOOK_DELETING_DESTRUCTOR` (`core/Hook.h`) | `BW_HOOK_METHOD(0x0045c610, CPUCheck, Run);` |
| Call an original function not yet decompiled | `CdeclFunction`, `StdcallFunction`, `ThiscallFunction` (`core/Call.h`) | `inline constexpr bw::CdeclFunction<0x0073445e, void*(std::size_t)> GameOperatorNew{};` |
| Read or write an original global | `BW_GLOBAL` (`core/Memory.h`) | `BW_GLOBAL(LHScreen, g_screen, 0x00d5c100);` |
| Call Windows like the game does | `BW_IMPORT` on the game's import-table slot (`game/Imports.h`) | `imports::s_DirectDrawEnumerateA(...)` |
| Allocate memory original code may free | `bw::GameAllocate`, `bw::GameAllocated` (`core/GameHeap.h`) | `class CPUCheck : public bw::GameAllocated` |
| Prove a layout | `BW_ASSERT_OFFSET`, `BW_ASSERT_SIZE` (`core/Layout.h`) | `BW_ASSERT_OFFSET(CPUCheck, m_vendor, 0x13d4);` |

### Calling conventions

The original was compiled by MSVC 6: free functions are `__cdecl` unless declared otherwise, member functions
`__thiscall` (`this` in ECX, callee pops stack arguments), Windows callbacks `__stdcall`. The hook macros derive the
convention and the number of bytes the callee pops from the C++ signature and record them; `tools/check_hooks.py`
compares that against every `ret n` in the original function, and fails the build if they differ.

Member functions cannot be jumped to through a member-function pointer (for virtual functions that is a vcall thunk
that would dispatch back into the hooked vtable). Each method hook instead gets a static `__fastcall` thunk with an
unused EDX parameter — ABI-identical to `__thiscall` — that makes a qualified, non-virtual call. Results must come
back in registers (`void`, scalars, pointers, references); class types returned by value use a hidden pointer whose
position differs between the two conventions and are rejected at compile time.

### Objects and virtual tables

A decompiled class keeps the original layout (`BW_ASSERT_OFFSET` for every recovered member, `std::byte
m_unknownXX[n]` for gaps) and the original order of virtual functions, taken from the RTTI-recovered vtables
(`tools/rtti.py`). Once a class's constructor is decompiled, new objects get the decompiled vtable; objects built by
original code keep the original vtable, whose slots reach the decompiled functions through their hooks.

### Two C runtimes

The game links the MSVC 6 CRT statically; the DLL links the current CRT statically. They have separate heaps,
`errno`s and file tables. Rules:

- memory that crosses the boundary comes from the game heap (`GameAllocate`/`GameFree`, `GameAllocated`);
- decompiled code never throws into original code: hook thunks are `noexcept`;
- floating-point code in the DLL uses SSE2, so it does not depend on the x87 precision Direct3D sets.

## Diagnostics

There is no debugger in the loop, so `src/diagnostics` makes the original code observable from `BWDecomp.log`
(`[Diagnostics]` in `BWDecomp.ini`): callers of `ExitProcess`, `PostQuitMessage` and `MessageBoxA` with their return
addresses, every exception with registers and a scan of return addresses into `.text`, and optionally the game's own
`OutputDebugString` output (LH3D and the game log a great deal). Map addresses to functions with
`symbols/functions.csv`. This is how the start-up failures in `docs/FIXES.md` were found.

For hangs, `tools/threads.py <pid>` suspends each thread of the running game in turn and prints its registers and
the return addresses on its stack, named from `symbols/functions.csv`; the shutdown watchdog logs the same from
inside the process. Full-screen DirectDraw cannot be captured from outside, so `LHScreen::Flip` saves the frame
itself when `tools/screenshot.py <pid>` signals it (or `ScreenshotKey` is pressed): the way to check rendering
changes without looking at the screen. `TraceDirectX` counts every DirectDraw/Direct3D call per calling function
([RENDERER.md](RENDERER.md)).

## Symbols

`symbols/functions.csv` is the decompilation's map. It is generated by `tools/build_symbols.py` from:

- Ghidra's function list (through REA), plus `symbols/manual_functions.csv` for entries it misses;
- MSVC RTTI (`tools/rtti.py`): 924 classes with base classes and vtables, so virtual functions and
  constructors/destructors get class names (`Creature::vf123`, `Villager::ctor_dtor_...`);
- `__FILE__` strings from assertions (`C:\dev\black\Creature.cpp`): 902 functions reference one directly, and since
  MSVC 6 emits each object file contiguously, functions between two anchors of the same file belong to it too
  (marked `File.cpp?`). 3,994 functions currently have a source file.

Regenerating keeps curated names, statuses and notes.

## End state

When every function of `.text` is decompiled, `runblack.exe` contributes only its data sections and resources. At that
point the DLL becomes a standalone executable: link the decompiled code with the original `.data`/`.rdata` layout
(extracted from the user's copy at build time, never committed), drop the SafeDisc remnant sections, and remove the
hook layer.
