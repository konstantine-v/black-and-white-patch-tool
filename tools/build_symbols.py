"""Build the function database (symbols/functions.csv) for runblack.exe.

Inputs: the image, REA's procedure list (orig/rea_procedures.json) and RTTI (orig/rtti.json).
For every function it records:
  * end      - next function start (upper bound of the body);
  * tu       - original translation unit, from references to __FILE__ strings ("Creature.cpp") in its body;
  * class    - owning class: lowest class in the hierarchy whose vtable holds it, or whose vtable it stores
               (constructor/destructor);
  * name     - Class::vfNNN for virtual slots, Class::ctor_dtor for vtable writers, else FUN_xxxxxxxx.
Existing names and statuses in an older functions.csv are preserved, so the file can be regenerated safely.

Function starts the analysis missed (e.g. callbacks only referenced as data) are added from
symbols/manual_functions.csv next to the output.

Usage: python -I build_symbols.py <image.exe> <rea_procedures.json> <rtti.json> <functions.csv>
"""
import csv
import json
import os
import re
import struct
import sys

import capstone
import pefile
from capstone import x86

FIELDS = ["address", "end", "name", "class", "tu", "status", "notes"]


def main(image_path: str, procs_path: str, rtti_path: str, out_path: str) -> None:
    pe = pefile.PE(image_path, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    mem = pe.get_memory_mapped_image()
    text = next(s for s in pe.sections if s.Name.startswith(b".text"))
    text_lo = base + text.VirtualAddress
    text_hi = text_lo + text.Misc_VirtualSize

    starts = sorted({int(p["address"], 16) for p in json.load(open(procs_path))["normalized_result"]
                     if not p["procedure"]["external"] and p["address"].startswith("0x")})
    starts = [s for s in starts if text_lo <= s < text_hi]
    manual_path = os.path.join(os.path.dirname(out_path), "manual_functions.csv")
    manual = {}
    if os.path.exists(manual_path):
        manual = {int(r["address"], 16): r for r in csv.DictReader(open(manual_path, newline=""))}
        starts = sorted(set(starts) | set(manual))

    # __FILE__ strings: "<name>.cpp" / ".c" as complete C strings.
    files = {}
    for m in re.finditer(rb"(?<=\0)([\x21-\x7e][\x20-\x7e]{1,200}?\.(?:cpp|c))\0", mem, re.I):
        files[base + m.start(1)] = os.path.basename(m.group(1).decode("latin-1").replace("\\", "/"))

    classes = json.load(open(rtti_path))
    depth = {c["name"]: len(c["bases"]) for c in classes}
    vt_owner = {}
    slot_owner: dict[int, tuple[str, int]] = {}
    for c in classes:
        for vt in c["vtables"]:
            vt_owner[int(vt["va"], 16)] = c["name"]
            if vt["offset"] != 0:
                continue
            for i, m in enumerate(vt["methods"]):
                addr = int(m, 16)
                prev = slot_owner.get(addr)
                if prev is None or depth[c["name"]] < depth[prev[0]]:
                    slot_owner[addr] = (c["name"], i)

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    md.skipdata = True
    rows = []
    for i, start in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else text_hi
        end = min(end, start + 0x10000)
        tus: dict[str, int] = {}
        stored_vtables = set()
        code = mem[start - base:end - base]
        for ins in md.disasm(code, start):
            for op in ins.operands if ins.id != 0 else ():
                if op.type == x86.X86_OP_IMM:
                    if op.imm in files:
                        tus[files[op.imm]] = tus.get(files[op.imm], 0) + 1
                    if ins.mnemonic == "mov" and op.imm in vt_owner and ins.operands[0].type == x86.X86_OP_MEM:
                        stored_vtables.add(vt_owner[op.imm])
            if ins.mnemonic in ("ret", "int3") and ins.address + ins.size >= end:
                break
        tu = max(tus, key=tus.get) if tus else ""
        cls, name = "", f"FUN_{start:08x}"
        if start in slot_owner:
            cls, slot = slot_owner[start]
            name = f"{cls}::vf{slot:03d}"
        elif stored_vtables:
            cls = max(stored_vtables, key=lambda n: depth.get(n, 0))
            name = f"{cls}::ctor_dtor_{start:08x}"
        row = {"address": f"0x{start:08x}", "end": f"0x{end:08x}", "name": name, "class": cls,
               "tu": tu, "status": "original", "notes": ""}
        if start in manual:
            row.update({k: v for k, v in manual[start].items() if k != "address" and v})
        rows.append(row)

    # MSVC 6 emits each object file's code contiguously: a function lying between two functions anchored to the
    # same translation unit belongs to it too (marked with a trailing '?').
    anchored = [i for i, r in enumerate(rows) if r["tu"]]
    for a, b in zip(anchored, anchored[1:]):
        if rows[a]["tu"] == rows[b]["tu"]:
            for r in rows[a + 1:b]:
                r["tu"] = rows[a]["tu"] + "?"

    # Preserve curated names/status from an existing database.
    if os.path.exists(out_path):
        old = {r["address"]: r for r in csv.DictReader(open(out_path, newline=""))}
        generated = ("FUN_", )
        for r in rows:
            o = old.get(r["address"])
            if not o:
                continue
            curated_name = not (o["name"].startswith(generated) or "::vf" in o["name"] or "ctor_dtor" in o["name"])
            if curated_name:
                r.update({k: o[k] for k in ("name", "class") if o.get(k)})
            r["status"] = o["status"] or r["status"]
            r["notes"] = o["notes"] or r["notes"]
            r["tu"] = o.get("tu") or r["tu"]

    with open(out_path, "w", newline="") as f:
        w = csv.DictWriter(f, FIELDS)
        w.writeheader()
        w.writerows(rows)
    print(f"functions {len(rows)}, with tu {sum(1 for r in rows if r['tu'])}, "
          f"with class {sum(1 for r in rows if r['class'])}, __FILE__ strings {len(files)}")


if __name__ == "__main__":
    main(*sys.argv[1:5])
