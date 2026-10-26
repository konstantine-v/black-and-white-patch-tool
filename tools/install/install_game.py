"""Install Black & White 1.00 from the disc image without running its 2001 InstallShield setup.

Steps (all from the user's own disc image):
  1. extract the InstallShield cabinets (data1.hdr/.cab, data2.cab) with iscab_extract.py, MD5-verified;
  2. add the DLLs the setup copies loose from the disc root (LHLogR.dll, DSETUP.dll, debug MFC runtime);
  3. merge the disc's Audio folder (music banks are only on the disc);
  4. install the unprotected runblack.exe from the disc's crack folder (the build the patch supports; the
     cabinet's runblack.exe is wrapped in SafeDisc, which current Windows cannot run) after checking its SHA-256;
  5. write HKCU\\Software\\Lionhead Studios Ltd\\Black & White\\GameDir, which the game uses for profiles and saves
     (0x00715a20; without it they go to \\dev\\black). LHLogR.dll's registry helpers default to HKEY_CURRENT_USER,
     so no administrator rights are needed;
  6. optionally copy BWLaunch.exe, BWDecomp.dll and BWDecomp.ini from a build directory.

Usage:
    python -I install_game.py --iso BNW.iso --dest "G:/Games/Black & White" [--decomp build/Release] [--seven-zip 7z.exe]
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
import winreg
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import iscab_extract  # noqa: E402  (sibling module; -I keeps the script directory off sys.path)

SUPPORTED_RUNBLACK_SHA256 = "86a30b08ab2ea8a240403d37f8f0303cf65aa5cde8e7f673049ab01654ccf755"
DISC_ROOT_DLLS = ("LHLogR.dll", "DSETUP.dll", "MFC42D.DLL", "MFCO42D.DLL", "MSVCRTD.DLL")
REGISTRY_KEY = r"Software\Lionhead Studios Ltd\Black & White"
DECOMP_FILES = ("BWLaunch.exe", "BWDecomp.dll")


def run_7zip(seven_zip: Path, iso: Path, out: Path, *patterns: str) -> None:
    subprocess.run([str(seven_zip), "x", "-y", f"-o{out}", str(iso), *patterns], check=True,
                   stdout=subprocess.DEVNULL)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def merge_tree(source: Path, target: Path) -> int:
    count = 0
    for file in source.rglob("*"):
        if file.is_file():
            destination = target / file.relative_to(source)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, destination)
            count += 1
    return count


def write_registry(game_dir: Path) -> None:
    with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, REGISTRY_KEY, 0, winreg.KEY_WRITE) as key:
        winreg.SetValueEx(key, "GameDir", 0, winreg.REG_SZ, str(game_dir))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--dest", type=Path, required=True)
    parser.add_argument("--decomp", type=Path, help="build directory with BWLaunch.exe and BWDecomp.dll")
    parser.add_argument("--ini", type=Path, help="BWDecomp.ini to install (default: repository copy)")
    parser.add_argument("--seven-zip", type=Path, default=Path(r"C:\Program Files\7-Zip\7z.exe"))
    args = parser.parse_args()
    dest = args.dest.resolve()

    with tempfile.TemporaryDirectory(prefix="bw_install_") as temp:
        disc = Path(temp)
        print("Extracting the disc image ...")
        run_7zip(args.seven_zip, args.iso, disc, "data1.hdr", "data1.cab", "data2.cab", "crack", "Audio",
                 *DISC_ROOT_DLLS)

        print("Extracting the InstallShield cabinets ...")
        if iscab_extract.extract(disc / "data1.hdr", dest) != 0:
            print("cabinet extraction failed", file=sys.stderr)
            return 1
        (dest / "manifest.json").unlink(missing_ok=True)

        for dll in DISC_ROOT_DLLS:
            shutil.copy2(disc / dll, dest / dll)
        print(f"Copied {len(DISC_ROOT_DLLS)} DLLs from the disc root")
        print(f"Merged {merge_tree(disc / 'Audio', dest / 'Audio')} audio files from the disc")

        crack = disc / "crack" / "runblack.exe"
        if sha256(crack) != SUPPORTED_RUNBLACK_SHA256:
            print(f"{crack} is not the supported build", file=sys.stderr)
            return 1
        shutil.copy2(crack, dest / "runblack.exe")
        print("Installed the unprotected runblack.exe")

    write_registry(dest)
    print(f"HKCU\\{REGISTRY_KEY}\\GameDir = {dest}")

    if args.decomp:
        for name in DECOMP_FILES:
            shutil.copy2(args.decomp / name, dest / name)
        ini = args.ini or Path(__file__).resolve().parents[2] / "BWDecomp.ini"
        if not (dest / "BWDecomp.ini").exists():
            shutil.copy2(ini, dest / "BWDecomp.ini")
        print("Installed BWLaunch.exe, BWDecomp.dll and BWDecomp.ini")
    return 0


if __name__ == "__main__":
    sys.exit(main())
