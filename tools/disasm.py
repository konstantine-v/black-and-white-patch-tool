"""Disassemble a function (until the next function start in functions.csv) or N bytes at an address.

Usage: python -I disasm.py <image.exe> <functions.csv> <va> [max_bytes]
"""
import bisect
import csv
import sys

import capstone
import pefile


def main(image: str, functions: str, va: int, limit: int | None) -> None:
    pe = pefile.PE(image, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    mem = pe.get_memory_mapped_image()
    rows = list(csv.DictReader(open(functions, newline="")))
    starts = [int(r["address"], 16) for r in rows]
    i = bisect.bisect_right(starts, va)
    end = va + limit if limit else (starts[i] if i < len(starts) else va + 0x400)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for ins in md.disasm(mem[va - base:end - base], va):
        print(f"{ins.address:08x}  {ins.bytes.hex():<20} {ins.mnemonic} {ins.op_str}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4], 16) if len(sys.argv) > 4 else None)
