"""Print the threads of a running 32-bit process: registers and the likely call stack.

Usage: threads.py <pid | image name> [functions.csv]

Without a debugger this is how to see where a hung game is stuck. Each thread is suspended briefly; its stack is
scanned for return addresses (values just after a call instruction in an executable module) and, when
functions.csv is given, addresses in the game are named. Needs a 64-bit Python (Wow64 thread contexts).
"""

from __future__ import annotations

import csv
import ctypes
import ctypes.wintypes as wt
import subprocess
import sys

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)

PROCESS_ALL = 0x0410 | 0x0010 | 0x0400  # query information, VM read
THREAD_ACCESS = 0x0002 | 0x0008 | 0x0040  # suspend/resume, get context, query information
TH32CS_SNAPTHREAD = 0x4
WOW64_CONTEXT_FULL = 0x10000 | 0x1 | 0x2 | 0x4  # control, integer, segments


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
                ("th32OwnerProcessID", wt.DWORD), ("tpBasePri", wt.LONG), ("tpDeltaPri", wt.LONG),
                ("dwFlags", wt.DWORD)]


class WOW64_CONTEXT(ctypes.Structure):
    _fields_ = [("ContextFlags", wt.DWORD), ("Dr", wt.DWORD * 6), ("FloatSave", ctypes.c_byte * 112),
                ("SegGs", wt.DWORD), ("SegFs", wt.DWORD), ("SegEs", wt.DWORD), ("SegDs", wt.DWORD),
                ("Edi", wt.DWORD), ("Esi", wt.DWORD), ("Ebx", wt.DWORD), ("Edx", wt.DWORD), ("Ecx", wt.DWORD),
                ("Eax", wt.DWORD), ("Ebp", wt.DWORD), ("Eip", wt.DWORD), ("SegCs", wt.DWORD),
                ("EFlags", wt.DWORD), ("Esp", wt.DWORD), ("SegSs", wt.DWORD), ("ExtendedRegisters", ctypes.c_byte * 512)]


class MODULEINFO(ctypes.Structure):
    _fields_ = [("lpBaseOfDll", ctypes.c_void_p), ("SizeOfImage", wt.DWORD), ("EntryPoint", ctypes.c_void_p)]


kernel32.OpenProcess.restype = wt.HANDLE
kernel32.OpenThread.restype = wt.HANDLE
kernel32.CreateToolhelp32Snapshot.restype = wt.HANDLE
kernel32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                       ctypes.POINTER(ctypes.c_size_t)]


def find_pid(name: str) -> int:
    out = subprocess.run(["tasklist", "/FO", "CSV", "/NH", "/FI", f"IMAGENAME eq {name}"], capture_output=True,
                         text=True).stdout
    for row in csv.reader(out.splitlines()):
        if len(row) > 1 and row[0].lower() == name.lower():
            return int(row[1])
    raise SystemExit(f"{name} is not running")


def modules(process) -> list[tuple[int, int, str]]:
    handles = (wt.HMODULE * 1024)()
    needed = wt.DWORD()
    LIST_MODULES_32BIT = 1
    if not psapi.EnumProcessModulesEx(process, handles, ctypes.sizeof(handles), ctypes.byref(needed),
                                      LIST_MODULES_32BIT):
        return []
    result = []
    for module in handles[:needed.value // ctypes.sizeof(wt.HMODULE)]:
        info = MODULEINFO()
        psapi.GetModuleInformation(process, wt.HMODULE(module), ctypes.byref(info), ctypes.sizeof(info))
        name = ctypes.create_unicode_buffer(260)
        psapi.GetModuleBaseNameW(process, wt.HMODULE(module), name, 260)
        result.append((info.lpBaseOfDll or 0, info.SizeOfImage, name.value))
    return result


def read(process, address: int, size: int) -> bytes:
    buffer = ctypes.create_string_buffer(size)
    done = ctypes.c_size_t()
    kernel32.ReadProcessMemory(process, ctypes.c_void_p(address), buffer, size, ctypes.byref(done))
    return buffer.raw[:done.value]


def load_names(path: str) -> list[tuple[int, str]]:
    names = []
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            names.append((int(row["address"], 16), row["name"]))
    names.sort()
    return names


def describe(address: int, mods, names) -> str | None:
    for base, size, name in mods:
        if base <= address < base + size:
            if name.lower() == "runblack.exe" and names:
                import bisect
                i = bisect.bisect_right(names, (address, "￿")) - 1
                if i >= 0:
                    return f"{names[i][1]}+0x{address - names[i][0]:x}"
            return f"{name}+0x{address - base:x}"
    return None


def is_after_call(process, address: int) -> bool:
    code = read(process, address - 7, 7)
    if len(code) < 7:
        return False
    return (code[2] == 0xE8  # call rel32
            or code[1] == 0xFF and (code[2] >> 3) & 7 == 2  # call [disp32] / call [reg+disp8]...
            or code[5] == 0xFF and (code[6] >> 3) & 7 == 2 and code[6] >> 6 == 3  # call reg
            or code[4] == 0xFF and (code[5] >> 3) & 7 == 2  # call [reg+disp8]
            or code[3] == 0xFF and (code[4] >> 3) & 7 == 2)


def main() -> None:
    target = sys.argv[1]
    pid = int(target) if target.isdigit() else find_pid(target)
    names = load_names(sys.argv[2]) if len(sys.argv) > 2 else []
    process = kernel32.OpenProcess(PROCESS_ALL, False, pid)
    if not process:
        raise SystemExit(f"OpenProcess failed: {ctypes.get_last_error()}")
    mods = modules(process)

    snapshot = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    entry = THREADENTRY32()
    entry.dwSize = ctypes.sizeof(entry)
    threads = []
    ok = kernel32.Thread32First(snapshot, ctypes.byref(entry))
    while ok:
        if entry.th32OwnerProcessID == pid:
            threads.append(entry.th32ThreadID)
        ok = kernel32.Thread32Next(snapshot, ctypes.byref(entry))
    kernel32.CloseHandle(snapshot)

    for tid in threads:
        thread = kernel32.OpenThread(THREAD_ACCESS, False, tid)
        if not thread:
            print(f"thread {tid}: cannot open")
            continue
        kernel32.Wow64SuspendThread(thread)
        context = WOW64_CONTEXT()
        context.ContextFlags = WOW64_CONTEXT_FULL
        got = kernel32.Wow64GetThreadContext(thread, ctypes.byref(context))
        stack = read(process, context.Esp, 0x4000) if got else b""
        kernel32.ResumeThread(thread)
        kernel32.CloseHandle(thread)
        if not got:
            print(f"thread {tid}: no context ({ctypes.get_last_error()})")
            continue
        print(f"thread {tid}: eip={context.Eip:08x} {describe(context.Eip, mods, names) or ''}")
        print(f"  eax={context.Eax:08x} ebx={context.Ebx:08x} ecx={context.Ecx:08x} edx={context.Edx:08x} "
              f"esi={context.Esi:08x} edi={context.Edi:08x} ebp={context.Ebp:08x} esp={context.Esp:08x}")
        shown = 0
        for offset in range(0, len(stack) - 3, 4):
            value = int.from_bytes(stack[offset:offset + 4], "little")
            where = describe(value, mods, names)
            if where and is_after_call(process, value):
                print(f"  [esp+{offset:04x}] {value:08x} {where}")
                shown += 1
                if shown >= 24:
                    break
    kernel32.CloseHandle(process)


if __name__ == "__main__":
    main()
