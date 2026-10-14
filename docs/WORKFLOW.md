# Decompiling a function

## 1. Pick a target

Work by original translation unit: the `tu` column of `symbols/functions.csv` names the source file (a trailing `?`
means inferred from link order). Finishing a whole file, or a whole class, is better than scattered functions,
because the class layout and the file's globals only have to be recovered once.

## 2. Recover it

Static analysis only, on `orig/runblack.exe`:

- REA (Ghidra): `batch_decompile` for pseudocode, `xrefs`, `search_strings`. Pseudocode is a lead, not the truth:
  confirm calling convention, stack arguments and anything Ghidra marks "unreachable" in the assembly.
- `tools/disasm.py <exe> symbols/functions.csv <va> [bytes]` — Capstone disassembly to the next function.
- `tools/xref.py <exe> symbols/functions.csv <va>...` — immediate/displacement references and direct calls/jumps.
- `tools/cstr.py <exe> <va>...` — strings at addresses (Ghidra truncates its labels).
- `tools/find_imm.py <exe> symbols/functions.csv <value>...` — instructions using a constant.
- `orig/rtti.json` — classes, base classes and vtables (`tools/rtti.py`).

Record every recovered fact in the code: original address next to each declaration, offsets asserted with
`BW_ASSERT_OFFSET`, and every intentional behaviour change explained in a comment and in `docs/FIXES.md`.

## 3. Write it

- Put it in `src/game/black/<File>.cpp` or `src/game/lhlib/<File>.cpp` after the original file name; classes and
  members keep their original names where RTTI or strings give them.
- Write modern, readable C++: `std::string_view`, `std::array`, `enum class`, tables instead of `switch` ladders,
  bounded text helpers (`core/Text.h`) instead of `sprintf`/`strcat`. Keep the observable behaviour, including text
  the game writes, unless the change is a documented fix.
- Shared state stays where the original keeps it: `BW_GLOBAL` for globals, game-heap allocation for anything original
  code may free.
- Register the replacement with the matching `BW_HOOK_*` macro next to the definitions.
- Add tests for any logic that does not need the game process (`tests/GameTests.cpp`).
- Add the `.cpp` to `bw_game` in `CMakeLists.txt`.

## 4. Verify

```powershell
.\build.ps1 -UpdateSymbols
```

This builds with `/W4 /WX`, runs the unit tests, writes the hook manifest and runs `tools/check_hooks.py`, which
fails if a hook's calling convention or `ret n` disagrees with the original, or if the original function has a branch
into the five bytes the hook overwrites. With `-UpdateSymbols` verified functions are marked `decompiled` in
`symbols/functions.csv` and the progress figure is printed.

Then play the affected feature through `BWLaunch.exe`. If something regresses, list the suspect addresses under
`[Hooks] Disabled=` in `BWDecomp.ini` to fall back to the original code and narrow it down.
