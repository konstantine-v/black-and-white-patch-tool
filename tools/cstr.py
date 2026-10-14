"""Print C strings at virtual addresses of a PE image (repr-escaped).

Usage: python -I cstr.py <image.exe> <va> [<va> ...]
"""
import sys

import pefile


def main(image: str, addresses: list[int]) -> None:
    pe = pefile.PE(image, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    mem = pe.get_memory_mapped_image()
    for va in addresses:
        off = va - base
        print(f"0x{va:08x} {mem[off:mem.index(0, off)].decode('latin-1')!r}")


if __name__ == "__main__":
    main(sys.argv[1], [int(a, 16) for a in sys.argv[2:]])
