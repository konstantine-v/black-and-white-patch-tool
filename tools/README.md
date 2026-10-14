# Tools

Static analysis only, except `threads.py` and `screenshot.py`, which inspect a running game. Use an isolated Python 3.10+ with the pinned packages:

```
python -m venv E:/Tools/re-venv
E:/Tools/re-venv/Scripts/python -m pip install capstone==5.0.6 pefile==2024.8.26
```

| Script | Purpose |
|---|---|
| `pe_inventory.py <exe> <out.json>` | Headers, sections, Rich header, imports, RTTI names, source-file strings |
| `rtti.py <exe> <out.json>` | MSVC RTTI: classes, base classes, vtables and their methods |
| `build_symbols.py <exe> <rea_procedures.json> <rtti.json> <functions.csv>` | Generate/refresh `symbols/functions.csv` (keeps curated names and statuses) |
| `check_hooks.py <exe> <functions.csv> <hooks.csv> [--update]` | Verify hooks against the original; update progress |
| `disasm.py <exe> <functions.csv> <va> [bytes]` | Disassemble a function or a byte range |
| `xref.py <exe> <functions.csv> <va>...` | References to addresses (immediates, displacements, rel32 calls/jumps) |
| `cstr.py <exe> <va>...` | Print C strings at addresses |
| `find_imm.py <exe> <functions.csv> <value>...` | Instructions using given constants |
| `threads.py <pid or runblack.exe> [functions.csv]` | Registers and return addresses of every thread of the running game (64-bit Python) |
| `dx_methods.py [--cpp <out.inc>]` | DirectDraw/Direct3D 7 vtable layouts from the Windows SDK headers |
| `directx_report.py <BWDecomp.directx.csv> [functions.csv] [--markdown]` | Summary of a `TraceDirectX` run |
| `screenshot.py <pid or runblack.exe>` | Saves the game's next frame to `Screenshots\` and prints the path |
| `install/install_game.py --iso <BNW.iso> --dest <folder> [--decomp <build>]` | Install the game from the disc image (needs 7-Zip) |
| `install/iscab_extract.py <data1.hdr> <out>` | Extract InstallShield 5/6 cabinets with MD5 verification |

`rea_procedures.json` is REA's `list_procedures` output for `orig/runblack.exe` (Ghidra 12.1.4, default analysis).
Outputs derived from the executable go to `orig/`, which is never committed.
