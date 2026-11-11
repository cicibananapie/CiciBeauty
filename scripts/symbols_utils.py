# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

from __future__ import annotations

import ctypes
import hashlib
import json
import os
import re
import shutil
import string
import sys
import subprocess
import tempfile
import zipfile
from pathlib import Path


def _windows_fixed_drives() -> list[Path]:
    get_drive_type = ctypes.windll.kernel32.GetDriveTypeW
    get_drive_type.argtypes = [ctypes.c_wchar_p]
    get_drive_type.restype = ctypes.c_uint
    # DRIVE_FIXED excludes removable, optical, network and unavailable drives.
    return [
        Path(f"{letter}:/")
        for letter in string.ascii_uppercase
        if get_drive_type(f"{letter}:\\") == 3
    ]


def _windows_symbol_storage_root() -> Path:
    candidates = []
    for drive in _windows_fixed_drives():
        try:
            candidates.append((shutil.disk_usage(drive).free, drive))
        except OSError:
            continue
    for _, drive in sorted(candidates, key=lambda item: item[0], reverse=True):
        root = drive / "CiciSymbols"
        try:
            root.mkdir(exist_ok=True)
            with tempfile.TemporaryFile(dir=root) as probe:
                probe.write(b"\0")
                probe.flush()
        except OSError:
            continue
        return root
    raise OSError(
        "No writable fixed drive is available for CiciSymbols. "
        "Set CICI_SYMBOL_ROOT to a writable symbol storage directory."
    )


def default_symbol_storage_root() -> Path:
    configured = os.environ.get("CICI_SYMBOL_ROOT", "").strip()
    if configured:
        return Path(configured).expanduser().resolve()
    if sys.platform == "win32":
        return _windows_symbol_storage_root()
    return Path.home() / "Library/Application Support/CiciBuild/Symbols"


def validate_pdb_output_root(root: Path) -> None:
    # This path is embedded in PE CodeView records. Keep it outside user profiles.
    normalized = str(root).replace("\\", "/").casefold()
    if not re.match(r"^[a-z]:/", normalized) or any(
        part in ("users", "documents and settings") for part in normalized.split("/")
    ):
        raise ValueError("Linker PDB output must use an absolute, user-independent Windows path")


def file_record(path: Path, relative: str) -> dict:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return {"path": relative, "size": path.stat().st_size, "sha256": digest.hexdigest()}


def dwarf_uuids(path: Path) -> set[tuple[str, str]]:
    result = subprocess.run(["xcrun", "dwarfdump", "--uuid", str(path)],
                            check=True, capture_output=True, text=True)
    values = set(re.findall(r"UUID: ([0-9A-Fa-f-]+) \(([^)]+)\)", result.stdout))
    if not values:
        raise ValueError(f"No Mach-O UUID found: {path}")
    return {(uuid.upper(), arch) for uuid, arch in values}


def validate_symbol_pairs(items: list[Path], binaries: list[Path], platform: str) -> list[dict]:
    pairs = []
    uuids = {item: dwarf_uuids(item) for item in items} if platform == "macos" and binaries else {}
    for binary in binaries:
        if platform == "macos":
            expected = dwarf_uuids(binary)
            matches = [item for item, values in uuids.items() if expected <= values]
        else:
            data = binary.read_bytes()
            paths = set()
            offset = data.find(b"RSDS")
            while offset != -1:
                end = data.find(b"\0", offset + 24)
                if end != -1:
                    value = data[offset + 24:end].decode("utf-8", errors="replace")
                    if value.lower().endswith(".pdb"):
                        paths.add(value)
                offset = data.find(b"RSDS", offset + 4)
            if len(paths) != 1:
                raise ValueError(f"Expected one CodeView PDB path in {binary}, found {paths}")
            pdb = Path(next(iter(paths))).resolve()
            matches = [item for item in items if item.resolve() == pdb]
        if len(matches) != 1:
            raise ValueError(f"Expected one matching symbol file for {binary}, found {matches}")
        pair = {"binary": binary.name, "symbol": matches[0].name}
        if platform == "windows":
            pair["pdb_path"] = next(iter(paths))
        if platform == "macos":
            pair["uuids"] = [{"uuid": uuid, "architecture": arch} for uuid, arch in sorted(expected)]
        pairs.append(pair)
    return pairs

def should_require_symbols(config: str) -> bool:
    return config.lower() in ("release", "relwithdebinfo")


def current_platform_kind() -> str:
    if sys.platform == "win32":
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    return sys.platform


def collect_symbol_items(symbol_root: Path, platform_kind: str) -> list[Path]:
    if not symbol_root.exists():
        return []

    items: list[Path] = []
    if platform_kind == "windows":
        for path in sorted(symbol_root.iterdir()):
            if path.is_file() and path.suffix.lower() == ".pdb":
                items.append(path)
    elif platform_kind == "macos":
        for path in sorted(symbol_root.iterdir()):
            if path.name.endswith(".dSYM"):
                items.append(path)

    unique: list[Path] = []
    seen = set()
    for item in items:
        key = str(item.resolve())
        if key in seen:
            continue
        seen.add(key)
        unique.append(item)
    return unique


def copy_any(src: Path, dst_dir: Path) -> Path:
    dst_dir.mkdir(parents=True, exist_ok=True)
    dst = dst_dir / src.name
    if dst.exists():
        if dst.is_dir():
            shutil.rmtree(dst)
        else:
            dst.unlink()

    if src.is_dir():
        shutil.copytree(src, dst)
    else:
        shutil.copy2(src, dst)
    return dst


def zip_dir_contents(source_dir: Path, output_zip: Path) -> None:
    output_zip.parent.mkdir(parents=True, exist_ok=True)
    if output_zip.exists():
        output_zip.unlink()

    with zipfile.ZipFile(output_zip, "w", zipfile.ZIP_DEFLATED) as zf:
        for item in sorted(source_dir.rglob("*")):
            if item.is_file():
                arcname = item.relative_to(source_dir).as_posix()
                zf.write(item, arcname=arcname)


def package_symbols_sidecar(
    build_dir: Path,
    config: str,
    output_zip: Path,
    require_symbols: bool | None = None,
    symbol_root: Path | None = None,
    metadata_files: list[Path] | None = None,
    extra_symbol_items: list[Path] | None = None,
    binary_files: list[Path] | None = None,
    archive_dir: Path | None = None,
    build_metadata: dict | None = None,
) -> dict:
    build_dir = Path(build_dir).resolve()
    output_zip = Path(output_zip).resolve()
    symbol_root = (
        Path(symbol_root).resolve()
        if symbol_root is not None
        else build_dir.parent / "bin" / config
    )
    platform_kind = current_platform_kind()

    require = should_require_symbols(config) if require_symbols is None else require_symbols
    if not symbol_root.exists():
        msg = f"Symbol directory not found: {symbol_root}"
        if require:
            raise FileNotFoundError(msg)
        print(f"Warning: {msg}")
        return {"created": False, "zip": str(output_zip), "items": [], "symbol_root": str(symbol_root)}

    items = collect_symbol_items(symbol_root, platform_kind)
    for extra_item in extra_symbol_items or []:
        extra_item = Path(extra_item).resolve()
        if not extra_item.exists():
            raise FileNotFoundError(f"Additional symbol item not found: {extra_item}")
        items.append(extra_item)

    unique_items: list[Path] = []
    items_by_name: dict[str, Path] = {}
    for item in items:
        existing = items_by_name.get(item.name.casefold())
        if existing is not None:
            if existing.resolve() != item.resolve():
                raise ValueError(
                    f"Symbol item name collision: {existing} and {item}"
                )
            continue
        items_by_name[item.name.casefold()] = item
        unique_items.append(item)
    items = unique_items

    if not items:
        msg = f"No symbol files found in {symbol_root} for platform '{platform_kind}'"
        if require:
            raise FileNotFoundError(msg)
        print(f"Warning: {msg}")
        return {"created": False, "zip": str(output_zip), "items": [], "symbol_root": str(symbol_root)}

    pairs = validate_symbol_pairs(items, binary_files or [], platform_kind)
    if binary_files:
        selected = {pair["symbol"] for pair in pairs}
        selected.update(Path(item).name for item in extra_symbol_items or [])
        items = [item for item in items if item.name in selected]
    manifest = {
        "schema_version": 1, "platform": platform_kind, "configuration": config,
        "build": build_metadata or {}, "pairs": pairs,
        "files": [
            dict(file_record(file, (Path("symbols") / item.name / file.relative_to(item)).as_posix()
                             if item.is_dir() else f"symbols/{item.name}"),
                 original_path=str(file))
            for item in items
            for file in (sorted(path for path in item.rglob("*") if path.is_file())
                         if item.is_dir() else [item])
        ],
    }
    if archive_dir is not None:
        archive_dir = Path(archive_dir).resolve()
        archive_dir.mkdir(parents=True, exist_ok=True)
        for item in items:
            if item.parent.resolve() != archive_dir:
                copy_any(item, archive_dir)
        (archive_dir / "symbol-manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        for metadata_file in metadata_files or []:
            if Path(metadata_file).resolve().parent != archive_dir:
                copy_any(Path(metadata_file), archive_dir)
    with tempfile.TemporaryDirectory(prefix="cici-symbols-") as tmp_dir:
        tmp_root = Path(tmp_dir)
        stage_dir = tmp_root / "symbols"
        copied = [copy_any(item, stage_dir) for item in items]
        for metadata_file in metadata_files or []:
            copy_any(Path(metadata_file), tmp_root)
        (tmp_root / "symbol-manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        zip_dir_contents(tmp_root, output_zip)

    print("Symbols package   :", output_zip)
    print("Symbols source    :", symbol_root)
    print("Symbols collected :", ", ".join(str(x.name) for x in copied))

    return {
        "created": True,
        "zip": str(output_zip),
        "items": [str(x) for x in copied],
        "symbol_root": str(symbol_root),
    }
