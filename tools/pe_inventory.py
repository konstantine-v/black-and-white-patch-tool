"""Inventory a PE image: headers, sections, Rich header, imports, RTTI class names and source-path strings.

Usage: python -I pe_inventory.py <image.exe> <out.json>
"""
import json
import re
import sys

import pefile


def main(path: str, out: str) -> None:
    pe = pefile.PE(path)
    data = pe.__data__
    info = {
        "timestamp": pe.FILE_HEADER.TimeDateStamp,
        "image_base": hex(pe.OPTIONAL_HEADER.ImageBase),
        "entry": hex(pe.OPTIONAL_HEADER.ImageBase + pe.OPTIONAL_HEADER.AddressOfEntryPoint),
        "linker": f"{pe.OPTIONAL_HEADER.MajorLinkerVersion}.{pe.OPTIONAL_HEADER.MinorLinkerVersion}",
        "subsystem": pe.OPTIONAL_HEADER.Subsystem,
        "characteristics": hex(pe.FILE_HEADER.Characteristics),
        "sections": [
            {"name": s.Name.rstrip(b"\0").decode(), "va": hex(pe.OPTIONAL_HEADER.ImageBase + s.VirtualAddress),
             "vsize": hex(s.Misc_VirtualSize), "rawsize": hex(s.SizeOfRawData), "flags": hex(s.Characteristics)}
            for s in pe.sections
        ],
        "rich": None,
        "imports": {},
    }
    rich = pe.parse_rich_header()
    if rich:
        vals = rich["values"]
        info["rich"] = [{"prodid": vals[i] >> 16, "build": vals[i] & 0xFFFF, "count": vals[i + 1]}
                        for i in range(0, len(vals), 2)]
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        info["imports"][entry.dll.decode()] = [
            (imp.name.decode() if imp.name else f"#{imp.ordinal}") for imp in entry.imports]
    info["rtti_types"] = sorted({m.group(0).decode("latin-1") for m in re.finditer(rb"\.\?A[VU][\w@?$<>]+@@", data)})
    info["source_paths"] = sorted({m.group(0).decode("latin-1")
                                   for m in re.finditer(rb"[\w:\/. -]{3,}\.(?:cpp|c|h|hpp)\b", data, re.I)})
    with open(out, "w") as f:
        json.dump(info, f, indent=1)
    print(json.dumps({k: v for k, v in info.items() if k not in ("imports", "rtti_types", "source_paths")}, indent=1))
    print("imports:", {k: len(v) for k, v in info["imports"].items()})
    print("rtti types:", len(info["rtti_types"]), "source paths:", len(info["source_paths"]))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
