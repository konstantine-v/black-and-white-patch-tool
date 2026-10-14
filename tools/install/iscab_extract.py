"""Extract files from an InstallShield 5/6 cabinet set (dataN.hdr + dataN.cab).

Written from scratch for the Black & White UE port. Only the subset of the
format used by IS6-era installers is supported: the per-file descriptor table,
split files spanning volumes, raw-deflate chunk compression and the optional
byte obfuscation. Every extracted file is verified against the MD5 stored in
the header, and a manifest.json (path, size, sha256) is written next to the
output so later tooling can detect changed sources.

Usage:
    python -I iscab_extract.py <data1.hdr> <output_dir>
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path, PureWindowsPath

SIGNATURE = 0x28635349  # "ISc("
COMMON_HEADER_SIZE = 20
FILE_DESCRIPTOR_SIZE_V6 = 0x57

FLAG_SPLIT = 0x1
FLAG_OBFUSCATED = 0x2
FLAG_COMPRESSED = 0x4
FLAG_INVALID = 0x8


class CabError(Exception):
    pass


@dataclass(frozen=True)
class CommonHeader:
    signature: int
    version: int
    volume_info: int
    cab_descriptor_offset: int
    cab_descriptor_size: int

    @staticmethod
    def parse(data: bytes, offset: int = 0) -> "CommonHeader":
        if len(data) < offset + COMMON_HEADER_SIZE:
            raise CabError("truncated common header")
        header = CommonHeader(*struct.unpack_from("<5I", data, offset))
        if header.signature != SIGNATURE:
            raise CabError(f"bad signature 0x{header.signature:08x}")
        return header

    @property
    def major_version(self) -> int:
        # IS stores the version either as 0x0100600C-style or as a plain small number.
        if self.version >> 24 == 1:
            return (self.version >> 12) & 0xF
        if self.version >> 24 == 2 or self.version >> 24 == 4:
            major = self.version & 0xFFFF
            return major // 100 if major else 0
        return self.version


@dataclass(frozen=True)
class VolumeHeader:
    data_offset: int
    first_file_index: int
    last_file_index: int
    first_file_offset: int
    first_file_size_compressed: int
    last_file_offset: int
    last_file_size_compressed: int

    @staticmethod
    def parse(data: bytes) -> "VolumeHeader":
        p = COMMON_HEADER_SIZE
        (data_offset, _unknown, first_index, last_index) = struct.unpack_from("<4I", data, p)
        p += 16
        (first_off, _first_exp, first_comp, last_off, _last_exp, last_comp) = struct.unpack_from("<6Q", data, p)
        return VolumeHeader(data_offset, first_index, last_index, first_off, first_comp, last_off, last_comp)


@dataclass(frozen=True)
class FileDescriptor:
    index: int
    flags: int
    expanded_size: int
    compressed_size: int
    data_offset: int
    md5: bytes
    name: str
    directory: str
    volume: int


def read_cstr(data: bytes, offset: int) -> str:
    end = data.index(b"\0", offset)
    return data[offset:end].decode("cp1252")


MAX_FILE_GROUP_COUNT = 71
TARGET_DIR_PREFIX = "<TARGETDIR>"


@dataclass(frozen=True)
class FileGroup:
    name: str
    first_file: int
    last_file: int
    destination: str


def parse_file_groups(hdr: bytes) -> list[FileGroup]:
    """File groups carry the install destination; v6 file descriptors mostly have an empty directory."""
    cab = CommonHeader.parse(hdr).cab_descriptor_offset
    groups: list[FileGroup] = []
    for bucket_offset in struct.unpack_from(f"<{MAX_FILE_GROUP_COUNT}I", hdr, cab + 0x3E):
        offset = bucket_offset
        while offset:
            _name, descriptor_offset, next_offset = struct.unpack_from("<3I", hdr, cab + offset)
            d = cab + descriptor_offset
            name_offset, = struct.unpack_from("<I", hdr, d)
            first_file, last_file = struct.unpack_from("<2I", hdr, d + 0x16)
            destination_offset, = struct.unpack_from("<I", hdr, d + 0x3A)
            groups.append(FileGroup(
                name=read_cstr(hdr, cab + name_offset),
                first_file=first_file,
                last_file=last_file,
                destination=read_cstr(hdr, cab + destination_offset) if destination_offset else "",
            ))
            offset = next_offset
    return groups


def install_directories(hdr: bytes) -> dict[int, str]:
    """Map file index -> install-relative directory for groups installed under <TARGETDIR>."""
    mapping: dict[int, str] = {}
    for group in parse_file_groups(hdr):
        if not group.destination.startswith(TARGET_DIR_PREFIX):
            continue  # installer engine/support files are not game data
        relative = group.destination[len(TARGET_DIR_PREFIX):].lstrip("\\")
        for index in range(group.first_file, group.last_file + 1):
            mapping[index] = relative
    return mapping


def parse_descriptors(hdr: bytes) -> list[FileDescriptor]:
    common = CommonHeader.parse(hdr)
    if common.major_version < 6:
        # v5 uses a different descriptor layout; B&W ships v6, so fail loudly.
        raise CabError(f"unsupported InstallShield major version {common.major_version}")

    cab = common.cab_descriptor_offset
    file_table_offset, = struct.unpack_from("<I", hdr, cab + 0x0C)
    directory_count, = struct.unpack_from("<I", hdr, cab + 0x1C)
    file_count, file_table_offset2 = struct.unpack_from("<2I", hdr, cab + 0x28)

    table_base = cab + file_table_offset
    directory_offsets = struct.unpack_from(f"<{directory_count}I", hdr, table_base)
    directories = [read_cstr(hdr, table_base + off) for off in directory_offsets]

    descriptors: list[FileDescriptor] = []
    for index in range(file_count):
        p = table_base + file_table_offset2 + index * FILE_DESCRIPTOR_SIZE_V6
        flags, = struct.unpack_from("<H", hdr, p)
        expanded, compressed, data_offset = struct.unpack_from("<3Q", hdr, p + 0x02)
        md5 = hdr[p + 0x1A:p + 0x2A]
        name_offset, directory_index = struct.unpack_from("<IH", hdr, p + 0x3A)
        volume, = struct.unpack_from("<H", hdr, p + 0x55)
        if flags & FLAG_INVALID or name_offset == 0:
            continue
        if directory_index >= len(directories):
            raise CabError(f"file {index}: directory index {directory_index} out of range")
        descriptors.append(FileDescriptor(
            index=index,
            flags=flags,
            expanded_size=expanded,
            compressed_size=compressed,
            data_offset=data_offset,
            md5=md5,
            name=read_cstr(hdr, table_base + name_offset),
            directory=directories[directory_index],
            volume=volume,
        ))
    return descriptors


class VolumeSet:
    """Lazily loads dataN.cab volumes and serves the raw stored bytes of a file."""

    def __init__(self, hdr_path: Path) -> None:
        stem = hdr_path.stem.rstrip("0123456789")
        self._directory = hdr_path.parent
        self._stem = stem
        self._volumes: dict[int, tuple[bytes, VolumeHeader]] = {}

    def volume(self, number: int) -> tuple[bytes, VolumeHeader]:
        if number not in self._volumes:
            path = self._directory / f"{self._stem}{number}.cab"
            if not path.is_file():
                raise CabError(f"missing volume {path}")
            data = path.read_bytes()
            CommonHeader.parse(data)
            self._volumes[number] = (data, VolumeHeader.parse(data))
        return self._volumes[number]

    def stored_bytes(self, fd: FileDescriptor) -> bytes:
        remaining = fd.compressed_size if fd.flags & FLAG_COMPRESSED else fd.expanded_size
        number = fd.volume
        data, vh = self.volume(number)
        offset = fd.data_offset
        pieces: list[bytes] = []
        while remaining > 0:
            available = remaining
            if fd.flags & FLAG_SPLIT and fd.index == vh.last_file_index and vh.last_file_offset == offset:
                available = min(remaining, vh.last_file_size_compressed)
            piece = data[offset:offset + available]
            if len(piece) != available:
                raise CabError(f"{fd.name}: volume {number} truncated")
            pieces.append(piece)
            remaining -= available
            if remaining > 0:
                number += 1
                data, vh = self.volume(number)
                if vh.first_file_index != fd.index:
                    raise CabError(f"{fd.name}: volume {number} does not continue file {fd.index}")
                offset = vh.first_file_offset
        return b"".join(pieces)


def deobfuscate(buffer: bytes) -> bytes:
    out = bytearray(len(buffer))
    seed = 0
    for i, value in enumerate(buffer):
        rotated = ((value ^ 0xD5) >> 2 | (value ^ 0xD5) << 6) & 0xFF
        out[i] = (rotated - seed) & 0xFF
        seed = (seed + 1) % 0x47
    return bytes(out)


def inflate_chunks(stored: bytes, expected_size: int, name: str) -> bytes:
    out = bytearray()
    p = 0
    while p < len(stored) and len(out) < expected_size:
        if p + 2 > len(stored):
            raise CabError(f"{name}: truncated chunk header")
        chunk_size, = struct.unpack_from("<H", stored, p)
        p += 2
        chunk = stored[p:p + chunk_size]
        if len(chunk) != chunk_size:
            raise CabError(f"{name}: truncated chunk")
        p += chunk_size
        inflater = zlib.decompressobj(-zlib.MAX_WBITS)
        out += inflater.decompress(chunk) + inflater.flush()
    if len(out) != expected_size:
        raise CabError(f"{name}: expanded to {len(out)} bytes, expected {expected_size}")
    return bytes(out)


def safe_relative_path(*segments: str) -> Path:
    parts = [part for part in PureWindowsPath(*segments).parts if part not in ("", "\\", "/")]
    for part in parts:
        if part in (".", "..") or ":" in part:
            raise CabError(f"unsafe path component {part!r} in {segments}")
    return Path(*parts)


def extract(hdr_path: Path, out_dir: Path) -> int:
    hdr = hdr_path.read_bytes()
    descriptors = parse_descriptors(hdr)
    directories = install_directories(hdr)
    volumes = VolumeSet(hdr_path)
    out_dir.mkdir(parents=True, exist_ok=True)

    manifest: list[dict[str, object]] = []
    seen: dict[Path, str] = {}
    failures = 0
    external = 0
    for fd in descriptors:
        if fd.index not in directories:
            continue
        relative = safe_relative_path(directories[fd.index], fd.directory, fd.name)
        if not fd.flags & FLAG_COMPRESSED:
            # Uncompressed entries are shipped loose on the disc, not inside the cabinet.
            print(f"SKIP external (loose on disc): {relative}", file=sys.stderr)
            external += 1
            continue
        try:
            stored = volumes.stored_bytes(fd)
            if fd.flags & FLAG_OBFUSCATED:
                stored = deobfuscate(stored)
            content = inflate_chunks(stored, fd.expanded_size, fd.name) if fd.flags & FLAG_COMPRESSED else stored
            if any(fd.md5) and hashlib.md5(content).digest() != fd.md5:
                raise CabError(f"{relative}: MD5 mismatch")
        except CabError as error:
            print(f"FAIL {error}", file=sys.stderr)
            failures += 1
            continue

        sha256 = hashlib.sha256(content).hexdigest()
        key = Path(str(relative).lower())
        if key in seen:
            if seen[key] != sha256:
                print(f"WARN duplicate path with different content kept first: {relative}", file=sys.stderr)
            continue
        seen[key] = sha256

        target = out_dir / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
        manifest.append({"path": relative.as_posix(), "size": len(content), "sha256": sha256})

    manifest.sort(key=lambda entry: str(entry["path"]).lower())
    (out_dir / "manifest.json").write_text(json.dumps({"source": hdr_path.name, "files": manifest}, indent=1))
    print(f"extracted {len(manifest)} files ({external} external, {failures} failures) to {out_dir}")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("hdr", type=Path, help="path to data1.hdr")
    parser.add_argument("out", type=Path, help="output directory")
    parser.add_argument("--list", action="store_true", help="list files without extracting")
    args = parser.parse_args()
    if args.list:
        for fd in parse_descriptors(args.hdr.read_bytes()):
            print(f"{fd.volume} {fd.flags:#04x} {fd.expanded_size:>10} {fd.directory}\\{fd.name}")
        return 0
    return extract(args.hdr, args.out)


if __name__ == "__main__":
    sys.exit(main())
