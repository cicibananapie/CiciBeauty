# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

from __future__ import annotations

import hashlib
import http.client
import json
import os
import shutil
import stat
import sys
import tarfile
import tempfile
import time
import urllib.parse
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable


SCHEMA_VERSION = 2
SUPPORTED_ARCHIVES = {"zip", "tar.gz", "file"}
SUPPORTED_PROVIDER = "github_release"


@dataclass(frozen=True)
class InstallTarget:
    source: PurePosixPath
    destination: PurePosixPath


@dataclass(frozen=True)
class Artifact:
    name: str
    repository: str
    tag: str
    asset: str
    sha256: str
    archive: str
    install: tuple[InstallTarget, ...]


@dataclass(frozen=True)
class DependencyConfig:
    platform: str
    architecture: str
    artifacts: tuple[Artifact, ...]


def _require_string(value: object, field: str, *, allow_empty: bool = False) -> str:
    if not isinstance(value, str) or (not allow_empty and not value.strip()):
        raise ValueError(f"{field} must be a non-empty string")
    return value.strip()


def _relative_path(value: object, field: str) -> PurePosixPath:
    text = _require_string(value, field)
    path = PurePosixPath(text.replace("\\", "/"))
    if (
        path.is_absolute()
        or ".." in path.parts
        or path == PurePosixPath(".")
        or (path.parts and path.parts[0].endswith(":"))
    ):
        raise ValueError(f"{field} must be a safe relative path: {text!r}")
    return path


def _load_install_targets(value: object, artifact_name: str) -> tuple[InstallTarget, ...]:
    if not isinstance(value, list) or not value:
        raise ValueError(f"artifact {artifact_name!r} install must be a non-empty array")

    targets: list[InstallTarget] = []
    for index, item in enumerate(value):
        field = f"artifacts.{artifact_name}.install[{index}]"
        if not isinstance(item, dict):
            raise ValueError(f"{field} must be an object")
        targets.append(
            InstallTarget(
                source=_relative_path(item.get("source"), f"{field}.source"),
                destination=_relative_path(
                    item.get("destination"), f"{field}.destination"
                ),
            )
        )
    return tuple(targets)


def load_dependency_config(
    path: Path, *, expected_platform: str | None = None
) -> DependencyConfig:
    with path.open(encoding="utf-8") as file:
        data = json.load(file)

    if not isinstance(data, dict):
        raise ValueError(
            f"invalid dependency configuration {path}: expected schema version 2 object"
        )
    if data.get("schema_version") != SCHEMA_VERSION:
        raise ValueError(
            f"unsupported dependency schema in {path}: "
            f"expected {SCHEMA_VERSION}, got {data.get('schema_version')!r}"
        )

    platform = _require_string(data.get("platform"), "platform")
    if expected_platform is not None and platform != expected_platform:
        raise ValueError(
            f"dependency platform mismatch in {path}: "
            f"expected {expected_platform!r}, got {platform!r}"
        )
    architecture = _require_string(data.get("architecture"), "architecture")
    artifact_data = data.get("artifacts")
    if not isinstance(artifact_data, dict) or not artifact_data:
        raise ValueError("artifacts must be a non-empty object")

    artifacts: list[Artifact] = []
    destinations: list[tuple[str, PurePosixPath]] = []
    for name, item in artifact_data.items():
        name = _require_string(name, "artifact name")
        field = f"artifacts.{name}"
        if not isinstance(item, dict):
            raise ValueError(f"{field} must be an object")
        provider = _require_string(item.get("provider"), f"{field}.provider")
        if provider != SUPPORTED_PROVIDER:
            raise ValueError(
                f"unsupported provider for artifact {name!r}: {provider!r}"
            )
        repository = _require_string(item.get("repository"), f"{field}.repository")
        if repository.count("/") != 1 or "://" in repository:
            raise ValueError(
                f"{field}.repository must use the GitHub owner/repository form"
            )
        tag = _require_string(item.get("tag"), f"{field}.tag")
        if tag.casefold() == "latest":
            raise ValueError(f"{field}.tag must be immutable; 'latest' is forbidden")
        asset = _require_string(item.get("asset"), f"{field}.asset")
        asset_path = PurePosixPath(asset.replace("\\", "/"))
        if (
            asset_path.name != asset
            or asset_path.is_absolute()
            or asset_path in (PurePosixPath("."), PurePosixPath(".."))
        ):
            raise ValueError(f"{field}.asset must be a file name")
        sha256 = _require_string(
            item.get("sha256"), f"{field}.sha256", allow_empty=True
        ).lower()
        archive = _require_string(item.get("archive"), f"{field}.archive")
        if archive not in SUPPORTED_ARCHIVES:
            raise ValueError(
                f"unsupported archive type for artifact {name!r}: {archive!r}"
            )
        install = _load_install_targets(item.get("install"), name)
        for target in install:
            for other_name, other in destinations:
                if (
                    target.destination == other
                    or target.destination in other.parents
                    or other in target.destination.parents
                ):
                    raise ValueError(
                        f"install destination conflict between {name!r} and "
                        f"{other_name!r}: {target.destination} and {other}"
                    )
            destinations.append((name, target.destination))
        artifacts.append(
            Artifact(
                name=name,
                repository=repository,
                tag=tag,
                asset=asset,
                sha256=sha256,
                archive=archive,
                install=install,
            )
        )

    return DependencyConfig(platform, architecture, tuple(artifacts))


def validate_artifact_ready(artifact: Artifact) -> None:
    if len(artifact.sha256) != 64 or any(
        character not in "0123456789abcdef" for character in artifact.sha256
    ):
        detail = "is empty" if not artifact.sha256 else "is not a 64-character hex digest"
        raise ValueError(
            f"artifact {artifact.name!r} sha256 {detail}; "
            "publish the immutable asset and update its SHA-256 before building"
        )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def default_cache_root() -> Path:
    configured = os.environ.get("CICI_BUILD_CACHE_ROOT")
    if configured:
        return Path(configured).expanduser().resolve()
    if sys.platform == "win32":
        local_app_data = os.environ.get("LOCALAPPDATA")
        if local_app_data:
            return Path(local_app_data) / "CiciBuildCache"
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Caches" / "CiciBuildCache"
    return Path.home() / ".cache" / "CiciBuildCache"


def download_github_release(
    artifact: Artifact, destination: Path, *, attempts: int = 3
) -> None:
    tag = urllib.parse.quote(artifact.tag, safe="")
    asset = urllib.parse.quote(artifact.asset, safe="")
    request = urllib.request.Request(
        f"https://github.com/{artifact.repository}/releases/download/{tag}/{asset}",
        headers={"User-Agent": "CiciBeauty-build", "Accept": "application/octet-stream"},
    )
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=f".{artifact.name}-download-", dir=destination.parent
    ) as temporary_directory:
        downloaded = Path(temporary_directory) / artifact.asset
        for attempt in range(1, attempts + 1):
            try:
                with urllib.request.urlopen(request, timeout=60) as response:
                    with downloaded.open("wb") as output:
                        shutil.copyfileobj(response, output, length=1024 * 1024)
                break
            except (OSError, http.client.HTTPException) as error:
                if attempt == attempts:
                    raise RuntimeError(
                        f"failed to download {artifact.repository} release "
                        f"{artifact.tag!r} asset {artifact.asset!r} after "
                        f"{attempts} attempts: {error}"
                    ) from error
                print(
                    f"Download attempt {attempt} failed for {artifact.name}; retrying..."
                )
                time.sleep(attempt)

        os.replace(downloaded, destination)


def obtain_artifact(
    artifact: Artifact, *, cache_root: Path, offline: bool = False
) -> Path:
    validate_artifact_ready(artifact)
    cache_path = cache_root / "sha256" / artifact.sha256 / artifact.asset
    if cache_path.is_file():
        actual = sha256_file(cache_path)
        if actual == artifact.sha256:
            print(f"Using cached artifact: {artifact.name} ({artifact.sha256})")
            return cache_path
        print(f"Removing corrupt cached artifact: {cache_path}")
        cache_path.unlink()

    if offline:
        raise FileNotFoundError(
            f"artifact {artifact.name!r} is not available in the verified cache: "
            f"{cache_path}"
        )

    cache_path.parent.mkdir(parents=True, exist_ok=True)
    pending = cache_path.with_name(f".{cache_path.name}.pending-{os.getpid()}")
    try:
        download_github_release(artifact, pending)
        actual = sha256_file(pending)
        if actual != artifact.sha256:
            raise ValueError(
                f"SHA-256 mismatch for {artifact.name!r}: "
                f"expected {artifact.sha256}, got {actual}"
            )
        os.replace(pending, cache_path)
    finally:
        if pending.exists():
            pending.unlink()
    print(f"Cached verified artifact: {artifact.name} ({artifact.sha256})")
    return cache_path


def _safe_archive_path(name: str, *, field: str) -> PurePosixPath:
    normalized = name.replace("\\", "/")
    path = PurePosixPath(normalized)
    if not normalized or path.is_absolute() or ".." in path.parts:
        raise ValueError(f"unsafe {field} in dependency archive: {name!r}")
    if path.parts and path.parts[0].endswith(":"):
        raise ValueError(f"unsafe {field} in dependency archive: {name!r}")
    return path


def _extract_zip(archive_path: Path, destination: Path) -> None:
    with zipfile.ZipFile(archive_path, "r") as archive:
        members = archive.infolist()
        if not members:
            raise ValueError(f"dependency archive is empty: {archive_path}")
        for member in members:
            _safe_archive_path(member.filename, field="ZIP member path")
            unix_mode = member.external_attr >> 16
            if stat.S_ISLNK(unix_mode):
                raise ValueError(
                    f"ZIP symlinks are not supported in dependency artifacts: "
                    f"{member.filename!r}"
                )
        archive.extractall(destination, members)
        corrupt = archive.testzip()
        if corrupt is not None:
            raise zipfile.BadZipFile(
                f"CRC check failed for {corrupt!r} in {archive_path}"
            )


def _validate_tar_members(members: Iterable[tarfile.TarInfo]) -> list[tarfile.TarInfo]:
    validated = list(members)
    if not validated:
        raise ValueError("dependency TAR archive is empty")

    symlink_paths: set[PurePosixPath] = set()
    for member in validated:
        path = _safe_archive_path(member.name, field="TAR member path")
        if member.ischr() or member.isblk() or member.isfifo() or member.isdev():
            raise ValueError(f"unsupported special file in TAR archive: {member.name!r}")
        if member.issym():
            link = PurePosixPath(member.linkname.replace("\\", "/"))
            if link.is_absolute():
                raise ValueError(f"unsafe TAR symlink target: {member.linkname!r}")
            resolved_parts: list[str] = []
            for part in (path.parent / link).parts:
                if part in ("", "."):
                    continue
                if part == "..":
                    if not resolved_parts:
                        raise ValueError(
                            f"unsafe TAR symlink target: {member.linkname!r}"
                        )
                    resolved_parts.pop()
                else:
                    resolved_parts.append(part)
            symlink_paths.add(path)
        elif member.islnk():
            _safe_archive_path(member.linkname, field="TAR hard-link target")

    for member in validated:
        path = _safe_archive_path(member.name, field="TAR member path")
        if any(parent in symlink_paths for parent in path.parents):
            raise ValueError(
                f"TAR member is nested below an archive symlink: {member.name!r}"
            )
    return validated


def _extract_tar(archive_path: Path, destination: Path) -> None:
    with tarfile.open(archive_path, "r:gz") as archive:
        members = _validate_tar_members(archive.getmembers())
        archive.extractall(destination, members=members)


def stage_artifact(artifact: Artifact, archive_path: Path, stage: Path) -> None:
    stage.mkdir(parents=True, exist_ok=True)
    if artifact.archive == "zip":
        _extract_zip(archive_path, stage)
    elif artifact.archive == "tar.gz":
        _extract_tar(archive_path, stage)
    elif artifact.archive == "file":
        shutil.copy2(archive_path, stage / artifact.asset)
    else:
        raise ValueError(f"unsupported archive type: {artifact.archive!r}")


def _remove_path(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.exists():
        shutil.rmtree(path)


def install_staged_artifacts(
    config: DependencyConfig, staged: dict[str, Path], depends_root: Path
) -> None:
    depends_root.mkdir(parents=True, exist_ok=True)
    operations: list[tuple[Path, Path]] = []
    for artifact in config.artifacts:
        artifact_stage = staged[artifact.name]
        for target in artifact.install:
            source = artifact_stage.joinpath(*target.source.parts)
            if not source.exists() and not source.is_symlink():
                raise FileNotFoundError(
                    f"artifact {artifact.name!r} does not contain install source "
                    f"{str(target.source)!r}"
                )
            destination = depends_root.joinpath(*target.destination.parts)
            operations.append((source, destination))

    with tempfile.TemporaryDirectory(
        prefix=".dependency-backup-", dir=depends_root.parent
    ) as backup_directory:
        backup_root = Path(backup_directory)
        backups: list[tuple[Path, Path]] = []
        installed: list[Path] = []
        try:
            for index, (source, destination) in enumerate(operations):
                destination.parent.mkdir(parents=True, exist_ok=True)
                if destination.exists() or destination.is_symlink():
                    backup = backup_root / str(index)
                    os.replace(destination, backup)
                    backups.append((backup, destination))
                os.replace(source, destination)
                installed.append(destination)
        except Exception:
            for destination in reversed(installed):
                _remove_path(destination)
            for backup, destination in reversed(backups):
                destination.parent.mkdir(parents=True, exist_ok=True)
                os.replace(backup, destination)
            raise


def install_dependencies(
    config: DependencyConfig,
    *,
    project_root: Path,
    cache_root: Path | None = None,
    offline: bool = False,
) -> None:
    resolved_cache_root = cache_root or default_cache_root()
    resolved_cache_root.mkdir(parents=True, exist_ok=True)

    for artifact in config.artifacts:
        validate_artifact_ready(artifact)

    with tempfile.TemporaryDirectory(
        prefix=".dependency-stage-", dir=project_root
    ) as temporary_directory:
        stage_root = Path(temporary_directory)
        staged: dict[str, Path] = {}
        for artifact in config.artifacts:
            archive_path = obtain_artifact(
                artifact, cache_root=resolved_cache_root, offline=offline
            )
            artifact_stage = stage_root / artifact.name
            print(f"Staging dependency artifact: {artifact.name}")
            stage_artifact(artifact, archive_path, artifact_stage)
            staged[artifact.name] = artifact_stage

        install_staged_artifacts(config, staged, project_root / "depends")
