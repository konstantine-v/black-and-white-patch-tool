"""Find instructions in .text whose immediate operands equal any of the given values (e.g. resolution limits).

Usage: python -I find_imm.py <image.exe> <functions.csv> <value> [<value> ...]   (values in hex or decimal)
"""
import bisect
import csv
import sys

import capstone
import pefile
from capstone import x86


def main(image: str, functions: str, values: set[int]) -> None:
    pe = pefile.PE(image, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    mem = pe.get_memory_mapped_image()
    rows = list(csv.DictReader(open(functions, newline="")))
    starts = [int(r["address"], 16) for r in rows]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    for i, start in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else start + 0x1000
        for ins in md.disasm(mem[start - base:end - base], start):
            if any(op.type == x86.X86_OP_IMM and op.imm in values for op in ins.operands):
                print(f"{ins.address:08x} {rows[i]['name']:<40} [{rows[i]['tu']}] {ins.mnemonic} {ins.op_str}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], {int(v, 0) for v in sys.argv[3:]})
