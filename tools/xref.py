"""List code that references given addresses: immediates/displacements (strings, globals, vtables) and E8/E9 rel32
calls or jumps.

Usage: python -I xref.py <image.exe> <functions.csv> <va> [<va> ...]
"""
import bisect
import csv
import struct
import sys

import pefile


def main(image: str, functions: str, targets: list[int]) -> None:
    pe = pefile.PE(image, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.startswith(b".text"))
    lo = base + text.VirtualAddress
    code = pe.get_memory_mapped_image()[text.VirtualAddress:text.VirtualAddress + text.Misc_VirtualSize]
    rows = list(csv.DictReader(open(functions, newline="")))
    starts = [int(r["address"], 16) for r in rows]
    for target in targets:
        needle = struct.pack("<I", target)
        pos = code.find(needle)
        while pos != -1:
            va = lo + pos
            i = bisect.bisect_right(starts, va) - 1
            owner = f'{rows[i]["name"]} [{rows[i]["tu"]}]' if i >= 0 else "?"
            print(f"0x{target:08x} <- 0x{va:08x} in {owner}")
            pos = code.find(needle, pos + 1)
        # Direct near calls/jumps: E8/E9 rel32.
        for pos in range(len(code) - 5):
            if code[pos] in (0xE8, 0xE9) and lo + pos + 5 + struct.unpack_from("<i", code, pos + 1)[0] == target:
                va = lo + pos
                i = bisect.bisect_right(starts, va) - 1
                kind = "call" if code[pos] == 0xE8 else "jmp"
                print(f"0x{target:08x} <- {kind} 0x{va:08x} in {rows[i]['name']} [{rows[i]['tu']}]")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], [int(a, 16) for a in sys.argv[3:]])
