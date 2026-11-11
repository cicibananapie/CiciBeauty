# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

"""The binary package boundary between the public plugin and private runtime."""

from __future__ import annotations

import json
import re
from pathlib import Path


def validate_runtime(root: Path, *, platform: str, version: str | None = None,
                     official: bool = False) -> dict:
    if platform not in ("macos", "windows"):
        raise ValueError(f"Unsupported runtime platform: {platform}")
    manifest = json.loads((root / "runtime-manifest.json").read_text(encoding="utf-8"))
    expected = {
        "schema_version": 1,
        "platform": platform,
        "architecture": "arm64" if platform == "macos" else "x64",
    }
    for field, value in expected.items():
        if manifest.get(field) != value:
            raise ValueError(f"Runtime {field} mismatch: expected {value!r}, got {manifest.get(field)!r}")
    runtime_version = manifest.get("version")
    if not isinstance(runtime_version, str) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", runtime_version):
        raise ValueError("Runtime version must have the form X.Y.Z")
    if official and version is not None and version != runtime_version:
        raise ValueError(f"Runtime version mismatch: expected {version!r}, got {runtime_version!r}")
    if manifest.get("configuration") not in ("Debug", "Release", "RelWithDebInfo"):
        raise ValueError("Runtime configuration is invalid")
    if not isinstance(manifest.get("official_build"), bool):
        raise ValueError("Runtime official_build must be a boolean")
    if platform == "macos":
        app = root / "ciciinstaller.app" / "Contents"
        required = [app / "MacOS" / "ciciinstaller"]
        required += [app / "Helpers" / name for name in (
            "ciciluma", "ciciupdater", "cicifeedback", "cicicrashreporter", "cicicrashhandler")]
        required += [app / "Helpers" / "cicirelay.app" / "Contents" / "MacOS" / "cicirelay"]
        resources = app / "Resources"
        forbidden = (app / "Frameworks" / "cicibeauty.so", app / "Helpers" / "cicibeautyStub.plugin")
    else:
        required = [root / "installer" / "ciciinstaller.exe", root / "root" / "cicirunner.exe",
                    root / "root" / "uninstall.exe"]
        required += [root / "slot" / name for name in (
            "ciciluma.dll", "ciciupdater.dll", "cicifeedback.dll", "cicirelay.dll",
            "cicicrashreporter.dll", "cicicrashhandler.dll", "ciciplayer.dll")]
        resources = root / "slot" / "Resources"
        forbidden = (root / "slot" / "cicibeauty.dll", root / "slot" / "cicibeautyStub.dll")
    for file in required + [resources / "defaultsetting.json"]:
        if not file.is_file():
            raise FileNotFoundError(f"Runtime package is incomplete: {file}")
    for directory in (resources / "effects", resources / "models"):
        if not directory.is_dir() or not any(directory.iterdir()):
            raise FileNotFoundError(f"Runtime resources are missing or empty: {directory}")
    for file in forbidden:
        if file.exists():
            raise ValueError(f"Runtime package must not contain the public plugin: {file}")
    return manifest
