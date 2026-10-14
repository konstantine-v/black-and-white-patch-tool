"""Recover MSVC RTTI from an x86 PE image: classes, base classes and virtual tables.

MSVC 6 layout: vftable[-1] -> RTTICompleteObjectLocator {sig, offset, cdOffset, TypeDescriptor*, ClassHierarchyDescriptor*};
TypeDescriptor {vftable*, spare, char name[]}; ClassHierarchyDescriptor {sig, attributes, numBaseClasses, BaseClassDescriptor**};
BaseClassDescriptor {TypeDescriptor*, numContainedBases, mdisp, pdisp, vdisp, attributes}.

Usage: python -I rtti.py <image.exe> <out.json>
"""
import ctypes
import json
import re
import struct
import sys

import pefile

_dbghelp = ctypes.WinDLL("dbghelp")
UNDNAME_TYPE_ONLY = 0x2000


def undecorate(mangled: str) -> str:
    """'.?AVFoo@Bar@@' -> 'Bar::Foo' via dbghelp."""
    buf = ctypes.create_string_buffer(1024)
    if _dbghelp.UnDecorateSymbolName(mangled[1:].encode(), buf, len(buf), UNDNAME_TYPE_ONLY):
        text = buf.value.decode("latin-1")
        return re.sub(r"^(class|struct|union) ", "", text)
    return mangled


class Image:
    def __init__(self, path: str):
        self.pe = pefile.PE(path, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.mem = bytearray(self.pe.get_memory_mapped_image())
        self.sections = {s.Name.rstrip(b"\0").decode(): (self.base + s.VirtualAddress,
                                                          self.base + s.VirtualAddress + s.Misc_VirtualSize)
                         for s in self.pe.sections}

    def u32(self, va: int) -> int:
        return struct.unpack_from("<I", self.mem, va - self.base)[0]

    def in_section(self, va: int, name: str) -> bool:
        lo, hi = self.sections[name]
        return lo <= va < hi

    def cstr(self, va: int) -> str:
        off = va - self.base
        return self.mem[off:self.mem.index(0, off)].decode("latin-1")


def main(path: str, out: str) -> None:
    img = Image(path)
    data_lo, data_hi = img.sections[".data"]
    # Type descriptors live in .data: name at +8.
    tds = {}
    for m in re.finditer(rb"\.\?A[VU][^\0]+@@\0", img.mem[data_lo - img.base:data_hi - img.base]):
        td = data_lo + m.start() - 8
        tds[td] = m.group(0)[:-1].decode("latin-1")

    rd_lo, rd_hi = img.sections[".rdata"]

    def is_ptr(va: int, section: str) -> bool:
        return img.sections[section][0] <= va < img.sections[section][1]

    # Complete object locators: sig 0, TypeDescriptor* known, CHD* in .rdata.
    cols = {}
    for va in range(rd_lo, rd_hi - 20, 4):
        sig, offset, cd, td, chd = struct.unpack_from("<5I", img.mem, va - img.base)
        if sig == 0 and td in tds and is_ptr(chd, ".rdata"):
            cols[va] = {"offset": offset, "td": td, "chd": chd}

    def bases_of(chd: int) -> list[dict]:
        _, attributes, count, array = struct.unpack_from("<4I", img.mem, chd - img.base)
        result = []
        for i in range(count):
            bcd = img.u32(array + 4 * i)
            td, contained, mdisp, pdisp, vdisp, attrs = struct.unpack_from("<Iiiii I", img.mem, bcd - img.base)
            result.append({"name": undecorate(tds.get(td, "?")), "contained": contained,
                           "mdisp": mdisp, "pdisp": pdisp, "vdisp": vdisp})
        return result

    col_set = set(cols)
    vtables = []
    for va in range(rd_lo, rd_hi - 4, 4):
        col = img.u32(va)
        if col not in col_set:
            continue
        vt = va + 4
        methods = []
        cur = vt
        while cur < rd_hi:
            target = img.u32(cur)
            if not is_ptr(target, ".text") or (cur != vt and img.u32(cur) in col_set):
                break
            # A following vtable's COL slot terminates this one.
            if cur != vt and img.u32(cur - 0) in col_set:
                break
            methods.append(target)
            cur += 4
            if img.u32(cur) in col_set:
                break
        vtables.append({"va": vt, "col": col, "methods": methods})

    classes = {}
    for td, mangled in tds.items():
        classes[td] = {"name": undecorate(mangled), "mangled": mangled, "type_descriptor": hex(td),
                       "bases": [], "vtables": []}
    for vt in vtables:
        col = cols[vt["col"]]
        cls = classes[col["td"]]
        if not cls["bases"]:
            cls["bases"] = bases_of(col["chd"])[1:]
        cls["vtables"].append({"va": hex(vt["va"]), "offset": col["offset"],
                               "methods": [hex(m) for m in vt["methods"]]})
    result = sorted(classes.values(), key=lambda c: c["name"])
    with open(out, "w") as f:
        json.dump(result, f, indent=1)
    with_vt = sum(1 for c in result if c["vtables"])
    print(f"type descriptors {len(tds)}, locators {len(cols)}, vtables {len(vtables)}, classes with vtables {with_vt}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
