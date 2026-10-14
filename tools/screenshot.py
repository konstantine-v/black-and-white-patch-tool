"""Ask a running game (with BWDecomp loaded) for a screenshot and print the file it saved.

Usage: screenshot.py <pid | image name> [timeout seconds]

Signals the event `Local\\BWDecomp.Screenshot.<pid>`; LHScreen::Flip saves the next frame to Screenshots\\ in the
game folder (see src/diagnostics/Screenshot.h). Full-screen DirectDraw cannot be captured from outside the game.
"""

from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import os
import sys
import time

from threads import find_pid  # same directory

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.OpenEventW.restype = wt.HANDLE
kernel32.OpenProcess.restype = wt.HANDLE
EVENT_MODIFY_STATE = 0x0002
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


def game_folder(pid: int) -> str:
    process = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not process:
        raise SystemExit(f"cannot open process {pid}")
    buffer = ctypes.create_unicode_buffer(32768)
    size = wt.DWORD(len(buffer))
    kernel32.QueryFullProcessImageNameW(process, 0, buffer, ctypes.byref(size))
    kernel32.CloseHandle(process)
    return os.path.dirname(buffer.value)


def main() -> None:
    target = sys.argv[1]
    timeout = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
    pid = int(target) if target.isdigit() else find_pid(target)
    folder = os.path.join(game_folder(pid), "Screenshots")
    before = set(os.listdir(folder)) if os.path.isdir(folder) else set()

    event = kernel32.OpenEventW(EVENT_MODIFY_STATE, False, f"Local\\BWDecomp.Screenshot.{pid}")
    if not event:
        raise SystemExit(f"no screenshot event in process {pid} (BWDecomp not loaded?): {ctypes.get_last_error()}")
    kernel32.SetEvent(event)
    kernel32.CloseHandle(event)

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if os.path.isdir(folder):
            new = sorted(set(os.listdir(folder)) - before)
            if new:
                path = os.path.join(folder, new[-1])
                time.sleep(0.5)  # let the game finish writing
                print(path)
                return
        time.sleep(0.2)
    raise SystemExit("no screenshot within the timeout (is the game minimised or not presenting frames?)")


if __name__ == "__main__":
    main()
