"""Verify decompiled-function hooks against the original executable and update decompilation progress.

For every hook in the manifest (written by `bw_tests.exe --write-hook-manifest`):
  * the address must start a function in functions.csv and the function must be at least 5 bytes long;
  * every `ret`/`ret n` in the original body must pop exactly the bytes the C++ signature declares
    (cdecl: plain `ret`; stdcall/thiscall: `ret n` with n = stack argument bytes);
  * no branch inside the original body may target the 5 bytes overwritten by the jump (offsets 1..4).
With --update, functions.csv gets status=decompiled (and the C++ name) for hooked functions that pass, and
status=original for functions no longer hooked. Exits non-zero when any check fails.

Usage: python -I check_hooks.py <image.exe> <functions.csv> <hooks.csv> [--update]
"""
import csv
import struct
import sys

import capstone
import pefile
from capstone import x86

JUMP_SIZE = 5
BRANCH_GROUPS = (capstone.CS_GRP_JUMP, capstone.CS_GRP_CALL)


def analyse(md: capstone.Cs, mem: bytes, base: int, start: int, end: int) -> tuple[set[int], list[int]]:
    """Follows control flow from `start` within [start, end) (including `jmp [reg*4 + table]` switch tables, whose
    data a linear sweep would misread as code). Returns the ret pop sizes and branches into the hooked bytes."""
    pops: set[int] = set()
    into_patch: list[int] = []
    seen: set[int] = set()
    work = [start]
    while work:
        address = work.pop()
        while start <= address < end and address not in seen:
            seen.add(address)
            ins = next(md.disasm(mem[address - base:end - base], address, count=1), None)
            if ins is None:
                break
            following = address + ins.size
            if ins.id in (x86.X86_INS_RET, x86.X86_INS_RETF):
                pops.add(ins.operands[0].imm if ins.operands else 0)
                break
            if ins.id == x86.X86_INS_INT3:
                break
            is_jump = ins.group(capstone.CS_GRP_JUMP)
            if (is_jump or ins.group(capstone.CS_GRP_CALL)) and ins.operands:
                operand = ins.operands[0]
                if operand.type == x86.X86_OP_IMM:
                    target = operand.imm
                    if start < target < start + JUMP_SIZE:
                        into_patch.append(ins.address)
                    if is_jump and start <= target < end:
                        work.append(target)
                elif ins.id == x86.X86_INS_JMP and operand.type == x86.X86_OP_MEM and operand.mem.scale == 4:
                    table = operand.mem.disp & 0xFFFFFFFF
                    while start <= table < end + 0x1000:
                        entry = struct.unpack_from("<I", mem, table - base)[0]
                        if not start <= entry < end:
                            break
                        if start < entry < start + JUMP_SIZE:
                            into_patch.append(ins.address)
                        work.append(entry)
                        table += 4
                if ins.id == x86.X86_INS_JMP:
                    break
            address = following
    return pops, into_patch


def main(image: str, functions_path: str, hooks_path: str, update: bool) -> int:
    pe = pefile.PE(image, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    mem = pe.get_memory_mapped_image()
    rows = list(csv.DictReader(open(functions_path, newline="")))
    by_address = {int(r["address"], 16): r for r in rows}
    hooks = list(csv.DictReader(open(hooks_path, newline="")))
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True

    failures = 0
    passed: dict[int, str] = {}
    for hook in hooks:
        address = int(hook["address"], 16)
        expected = int(hook["callee_stack_bytes"])
        label = f'0x{address:08x} {hook["name"]}'
        row = by_address.get(address)
        if row is None:
            print(f"FAIL {label}: not a function start in functions.csv")
            failures += 1
            continue
        end = int(row["end"], 16)
        if end - address < JUMP_SIZE:
            print(f"FAIL {label}: function is only {end - address} bytes")
            failures += 1
            continue
        pops, into_patch = analyse(md, mem, base, address, end)
        problems = []
        if pops and pops != {expected}:
            problems.append(f"original pops {sorted(pops)} bytes, C++ signature pops {expected}")
        if into_patch:
            problems.append("branches into the patched bytes from " + ", ".join(f"0x{a:08x}" for a in into_patch))
        if problems:
            print(f"FAIL {label}: " + "; ".join(problems))
            failures += 1
            continue
        note = "" if pops else " (no ret found: tail call or noreturn, convention unchecked)"
        print(f"ok   {label} [{hook['convention']}, ret {expected}]{note}")
        passed[address] = hook["name"]

    if update:
        for address, row in by_address.items():
            if address in passed:
                row["status"] = "decompiled"
                row["name"] = passed[address]
            elif row["status"] == "decompiled":
                row["status"] = "original"
        with open(functions_path, "w", newline="") as f:
            writer = csv.DictWriter(f, list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)

    total_bytes = sum(int(r["end"], 16) - int(r["address"], 16) for r in rows)
    done = [r for r in rows if int(r["address"], 16) in passed]
    done_bytes = sum(int(r["end"], 16) - int(r["address"], 16) for r in done)
    print(f"\n{len(passed)}/{len(hooks)} hooks verified; decompiled {len(done)}/{len(rows)} functions, "
          f"{done_bytes}/{total_bytes} bytes ({100.0 * done_bytes / total_bytes:.3f}%)")
    return 1 if failures else 0


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--update"]
    sys.exit(main(args[0], args[1], args[2], "--update" in sys.argv))
