# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

from __future__ import annotations

import hashlib
import io
import json
import os
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

from scripts import dependency_artifacts, pull_depends


REPOSITORY = "cicibananapie/CiciBeauty"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def artifact_config(
    *,
    sha256: str,
    archive: str = "zip",
    asset: str = "sdk.zip",
    source: str = "sdk",
    destination: str = "sdk",
) -> dict[str, object]:
    return {
        "provider": "github_release",
        "repository": REPOSITORY,
        "tag": "sdk/windows-x64/1.0.0-r1",
        "asset": asset,
        "sha256": sha256,
        "archive": archive,
        "install": [{"source": source, "destination": destination}],
    }


def write_config(
    path: Path,
    artifacts: dict[str, object],
    *,
    platform: str = "windows",
    architecture: str = "x64",
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(
            {
                "schema_version": 2,
                "platform": platform,
                "architecture": architecture,
                "artifacts": artifacts,
            }
        ),
        encoding="utf-8",
    )


class DependencyConfigurationTests(unittest.TestCase):
    def test_repository_configs_use_the_same_schema(self) -> None:
        project = Path(__file__).resolve().parents[2]
        windows = dependency_artifacts.load_dependency_config(
            project / "config/dependencies_win.json", expected_platform="windows"
        )
        macos = dependency_artifacts.load_dependency_config(
            project / "config/dependencies_mac.json", expected_platform="macos"
        )

        self.assertEqual("x64", windows.architecture)
        self.assertEqual("arm64", macos.architecture)
        self.assertEqual(
            {"qt_sdk", "obs_plugin_sdk", "vcrt"},
            {item.name for item in windows.artifacts},
        )
        obs_sdk = next(
            item for item in windows.artifacts if item.name == "obs_plugin_sdk"
        )
        self.assertEqual("build-dependencies-v1", obs_sdk.tag)
        self.assertEqual(
            "f6e54aa9062baea6c9235bc8a72df347cdd2c019ccf5dd3700655a82dfb687c7",
            obs_sdk.sha256,
        )
        self.assertEqual(
            dependency_artifacts.PurePosixPath("obs-sdk"),
            obs_sdk.install[0].destination,
        )
        self.assertEqual(
            {"qt_sdk", "obs_plugin_sdk"},
            {item.name for item in macos.artifacts},
        )
        macos_obs_sdk = next(
            item for item in macos.artifacts if item.name == "obs_plugin_sdk"
        )
        self.assertEqual("build-dependencies-v1", macos_obs_sdk.tag)
        self.assertEqual(
            "09382a2addcf9c8b7fc695f7e2e6e537e78ec6dddce3019505a0a74682ee79c7",
            macos_obs_sdk.sha256,
        )
        self.assertEqual(
            dependency_artifacts.PurePosixPath("obs-sdk"),
            macos_obs_sdk.install[0].destination,
        )
        macos_qt_sdk = next(item for item in macos.artifacts if item.name == "qt_sdk")
        self.assertEqual(
            "cae4825da6ef44931b10aef07d85d01fd3b22eeb4df6adefe4b7955d060e79a5",
            macos_qt_sdk.sha256,
        )

    def test_legacy_array_configuration_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "dependencies.json"
            path.write_text('[{"name":"obsQT","version":"latest"}]', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "schema version 2 object"):
                dependency_artifacts.load_dependency_config(path)

    def test_latest_tag_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "dependencies.json"
            item = artifact_config(sha256="0" * 64)
            item["tag"] = "latest"
            write_config(path, {"sdk": item})
            with self.assertRaisesRegex(ValueError, "latest.*forbidden"):
                dependency_artifacts.load_dependency_config(path)

    def test_empty_sha_is_loaded_but_cannot_be_installed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "dependencies.json"
            write_config(path, {"sdk": artifact_config(sha256="")})
            config = dependency_artifacts.load_dependency_config(path)

            with mock.patch.object(
                dependency_artifacts, "obtain_artifact"
            ) as obtain:
                with self.assertRaisesRegex(ValueError, "sha256 is empty"):
                    dependency_artifacts.install_dependencies(
                        config, project_root=root, cache_root=root / "cache"
                    )
            obtain.assert_not_called()


class ArtifactCacheTests(unittest.TestCase):
    def make_artifact(self, content: bytes) -> dependency_artifacts.Artifact:
        return dependency_artifacts.Artifact(
            name="sdk",
            repository=REPOSITORY,
            tag="sdk/windows-x64/1.0.0-r1",
            asset="sdk.zip",
            sha256=digest(content),
            archive="zip",
            install=(
                dependency_artifacts.InstallTarget(
                    source=dependency_artifacts.PurePosixPath("sdk"),
                    destination=dependency_artifacts.PurePosixPath("sdk"),
                ),
            ),
        )

    def test_verified_cache_avoids_download(self) -> None:
        content = b"verified"
        artifact = self.make_artifact(content)
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory)
            cached = cache / "sha256" / artifact.sha256 / artifact.asset
            cached.parent.mkdir(parents=True)
            cached.write_bytes(content)

            with mock.patch.object(
                dependency_artifacts, "download_github_release"
            ) as download:
                result = dependency_artifacts.obtain_artifact(
                    artifact, cache_root=cache
                )

            self.assertEqual(cached, result)
            download.assert_not_called()

    def test_download_hash_mismatch_is_rejected_and_not_cached(self) -> None:
        artifact = self.make_artifact(b"expected")
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory)

            def download(_artifact: object, destination: Path) -> None:
                destination.write_bytes(b"unexpected")

            with mock.patch.object(
                dependency_artifacts,
                "download_github_release",
                side_effect=download,
            ):
                with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                    dependency_artifacts.obtain_artifact(
                        artifact, cache_root=cache
                    )

            self.assertFalse(
                (cache / "sha256" / artifact.sha256 / artifact.asset).exists()
            )

    def test_offline_requires_verified_cache(self) -> None:
        artifact = self.make_artifact(b"expected")
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(FileNotFoundError, "verified cache"):
                dependency_artifacts.obtain_artifact(
                    artifact, cache_root=Path(directory), offline=True
                )


class ArchiveSafetyTests(unittest.TestCase):
    def test_zip_parent_traversal_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / "bad.zip"
            with zipfile.ZipFile(archive, "w") as output:
                output.writestr("../outside.txt", "bad")
            with self.assertRaisesRegex(ValueError, "unsafe ZIP member path"):
                dependency_artifacts._extract_zip(archive, root / "stage")
            self.assertFalse((root / "outside.txt").exists())

    def test_tar_parent_traversal_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / "bad.tar.gz"
            with tarfile.open(archive, "w:gz") as output:
                info = tarfile.TarInfo("../outside.txt")
                info.size = 3
                output.addfile(info, io.BytesIO(b"bad"))
            with self.assertRaisesRegex(ValueError, "unsafe TAR member path"):
                dependency_artifacts._extract_tar(archive, root / "stage")
            self.assertFalse((root / "outside.txt").exists())

    def test_tar_preserves_safe_symlink(self) -> None:
        if sys.platform == "win32":
            self.skipTest("tarfile may materialize symlinks without Windows link privileges")
        if not hasattr(os, "symlink"):
            self.skipTest("symlinks are not available")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / "sdk.tar.gz"
            with tarfile.open(archive, "w:gz") as output:
                data = b"library"
                file_info = tarfile.TarInfo("sdk/lib/libQt.dylib.6")
                file_info.size = len(data)
                output.addfile(file_info, io.BytesIO(data))
                link_info = tarfile.TarInfo("sdk/lib/libQt.dylib")
                link_info.type = tarfile.SYMTYPE
                link_info.linkname = "libQt.dylib.6"
                output.addfile(link_info)

            try:
                dependency_artifacts._extract_tar(archive, root / "stage")
            except OSError as error:
                self.skipTest(f"the test account cannot create symlinks: {error}")
            link = root / "stage/sdk/lib/libQt.dylib"
            self.assertTrue(link.is_symlink())
            self.assertEqual("libQt.dylib.6", os.readlink(link))


class DependencyInstallationTests(unittest.TestCase):
    def test_install_replaces_only_declared_destinations(self) -> None:
        zip_bytes = io.BytesIO()
        with zipfile.ZipFile(zip_bytes, "w") as output:
            output.writestr("obsQT/bin/Qt6Core.dll", "qt")
            output.writestr("QTTool/bin/lrelease.exe", "tool")
        content = zip_bytes.getvalue()

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config_path = root / "dependencies.json"
            item = artifact_config(sha256=digest(content))
            item["install"] = [
                {"source": "obsQT", "destination": "obsQT"},
                {"source": "QTTool", "destination": "QTTool"},
            ]
            write_config(config_path, {"qt_sdk": item})
            config = dependency_artifacts.load_dependency_config(config_path)

            effect = root / "depends/effectengine/keep.txt"
            old_qt = root / "depends/obsQT/old.txt"
            effect.parent.mkdir(parents=True)
            old_qt.parent.mkdir(parents=True)
            effect.write_text("keep", encoding="ascii")
            old_qt.write_text("old", encoding="ascii")

            def download(_artifact: object, destination: Path) -> None:
                destination.write_bytes(content)

            with mock.patch.object(
                dependency_artifacts,
                "download_github_release",
                side_effect=download,
            ):
                dependency_artifacts.install_dependencies(
                    config,
                    project_root=root,
                    cache_root=root / "cache",
                )

            self.assertEqual("keep", effect.read_text(encoding="ascii"))
            self.assertFalse(old_qt.exists())
            self.assertEqual(
                "qt",
                (root / "depends/obsQT/bin/Qt6Core.dll").read_text(
                    encoding="ascii"
                ),
            )
            self.assertEqual(
                "tool",
                (root / "depends/QTTool/bin/lrelease.exe").read_text(
                    encoding="ascii"
                ),
            )

    def test_missing_install_source_preserves_existing_dependencies(self) -> None:
        zip_bytes = io.BytesIO()
        with zipfile.ZipFile(zip_bytes, "w") as output:
            output.writestr("different/file.txt", "new")
        content = zip_bytes.getvalue()

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config_path = root / "dependencies.json"
            write_config(
                config_path,
                {"sdk": artifact_config(sha256=digest(content))},
            )
            config = dependency_artifacts.load_dependency_config(config_path)
            existing = root / "depends/sdk/keep.txt"
            existing.parent.mkdir(parents=True)
            existing.write_text("old", encoding="ascii")

            def download(_artifact: object, destination: Path) -> None:
                destination.write_bytes(content)

            with mock.patch.object(
                dependency_artifacts,
                "download_github_release",
                side_effect=download,
            ):
                with self.assertRaisesRegex(FileNotFoundError, "install source"):
                    dependency_artifacts.install_dependencies(
                        config,
                        project_root=root,
                        cache_root=root / "cache",
                    )

            self.assertEqual("old", existing.read_text(encoding="ascii"))

    def test_install_failure_rolls_back_every_replaced_destination(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config_path = root / "dependencies.json"
            item = artifact_config(sha256="0" * 64)
            item["install"] = [
                {"source": "one", "destination": "one"},
                {"source": "two", "destination": "two"},
            ]
            write_config(config_path, {"sdk": item})
            config = dependency_artifacts.load_dependency_config(config_path)

            stage = root / "stage/sdk"
            (stage / "one").mkdir(parents=True)
            (stage / "two").mkdir()
            (stage / "one/new.txt").write_text("new-one", encoding="ascii")
            (stage / "two/new.txt").write_text("new-two", encoding="ascii")
            old_one = root / "depends/one/old.txt"
            old_two = root / "depends/two/old.txt"
            old_one.parent.mkdir(parents=True)
            old_two.parent.mkdir(parents=True)
            old_one.write_text("old-one", encoding="ascii")
            old_two.write_text("old-two", encoding="ascii")

            real_replace = os.replace

            def fail_second_install(source: object, destination: object) -> None:
                if Path(source) == stage / "two":
                    raise PermissionError("simulated locked destination")
                real_replace(source, destination)

            with mock.patch.object(
                dependency_artifacts.os,
                "replace",
                side_effect=fail_second_install,
            ):
                with self.assertRaisesRegex(PermissionError, "simulated"):
                    dependency_artifacts.install_staged_artifacts(
                        config, {"sdk": stage}, root / "depends"
                    )

            self.assertEqual("old-one", old_one.read_text(encoding="ascii"))
            self.assertEqual("old-two", old_two.read_text(encoding="ascii"))
            self.assertFalse((root / "depends/one/new.txt").exists())
            self.assertFalse((root / "depends/two/new.txt").exists())

    def test_process_deps_selects_mac_schema_without_legacy_branch(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_config(
                root / "config/dependencies_mac.json",
                {"qt_sdk": artifact_config(sha256="")},
                platform="macos",
                architecture="arm64",
            )
            with mock.patch.object(pull_depends, "PROJECT_ROOT", root), mock.patch.object(
                pull_depends, "install_dependencies"
            ) as install:
                pull_depends.process_deps(
                    "macos", cache_root=root / "cache"
                )

            loaded = install.call_args.args[0]
            self.assertEqual("macos", loaded.platform)
            self.assertEqual("arm64", loaded.architecture)


if __name__ == "__main__":
    unittest.main()
