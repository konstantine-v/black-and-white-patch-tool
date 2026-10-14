# Tools

Static analysis only: nothing here runs game code. Use an isolated Python 3.10+ with the pinned packages:

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

`rea_procedures.json` is REA's `list_procedures` output for `orig/runblack.exe` (Ghidra 12.1.4, default analysis).
Outputs derived from the executable go to `orig/`, which is never committed.
