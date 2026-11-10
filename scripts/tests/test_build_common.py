# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

from __future__ import annotations

import sys
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPTS_DIR = Path(__file__).resolve().parents[1]
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from build_common import (
    CMakeBuildRequest,
    find_vs_environment,
    replace_deployment_artifact,
    write_deployment_pointer,
)


class BuildCommonTests(unittest.TestCase):
    def request(self) -> CMakeBuildRequest:
        return CMakeBuildRequest(
            project_root=Path("/project"),
            build_dir=Path("/project/build-test"),
            config="Release",
            version="1.2.3",
            channel_id="test",
            official=True,
            output_root=Path("/output"),
            pdb_output_root=Path("D:/symbols"),
        )

    def test_common_definitions_are_identical_for_both_generators(self) -> None:
        request = self.request()
        mac = request.configure_command(("-G", "Xcode"))
        windows = request.configure_command(("-G", "Visual Studio 17 2022"))
        for value in (
            "-DCICI_VERSION=1.2.3",
            "-DCICI_CHANNEL_ID=test",
            "-DOFFICIAL_BUILD=ON",
            "-DCICI_OUTPUT_ROOT=/output",
            "-DCICI_PDB_OUTPUT_ROOT=D:/symbols",
        ):
            self.assertIn(value, mac)
            self.assertIn(value, windows)


    def test_vs_explicit_path_is_validated_without_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "VsDevCmd.bat"
            script.touch()
            self.assertEqual(script, find_vs_environment(script))
            with mock.patch("build_common.shutil.which") as search:
                with self.assertRaises(FileNotFoundError):
                    find_vs_environment(script.parent / "missing.bat")
                search.assert_not_called()

    def test_vs_environment_override(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "VsDevCmd.bat"
            script.touch()
            with mock.patch.dict("build_common.os.environ", {"CICI_VSDEVCMD": str(script)}, clear=True):
                self.assertEqual(script, find_vs_environment())

    def test_vswhere_finds_custom_installation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "custom-build-tools"
            script = root / "Common7/Tools/VsDevCmd.bat"
            script.parent.mkdir(parents=True)
            script.touch()
            with (
                mock.patch.dict("build_common.os.environ", {}, clear=True),
                mock.patch("build_common.shutil.which", return_value="vswhere.exe"),
                mock.patch("build_common.subprocess.run", return_value=subprocess.CompletedProcess(
                    [], 0, stdout=str(root) + "\n")) as run,
            ):
                self.assertEqual(script, find_vs_environment())
            self.assertIn("[17.0,18.0)", run.call_args.args[0])

    def test_missing_visual_studio_reports_configuration_option(self) -> None:
        with mock.patch.dict("build_common.os.environ", {}, clear=True), mock.patch(
            "build_common.shutil.which", return_value=None
        ):
            with self.assertRaisesRegex(FileNotFoundError, "CICI_VSDEVCMD"):
                find_vs_environment()

    def test_build_command_enables_native_parallelism(self) -> None:
        self.assertIn("--parallel", self.request().build_command())

    def test_missing_deployment_artifact_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            missing = Path(directory) / "missing.dll"
            with self.assertRaisesRegex(FileNotFoundError, "deployment artifact"):
                replace_deployment_artifact(missing, Path(directory) / "target.dll")

    def test_deployment_pointer_creates_parent_and_replaces_value(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            pointer = Path(directory) / "nested" / "config"
            write_deployment_pointer(pointer, "first")
            write_deployment_pointer(pointer, "second")
            self.assertEqual("second", pointer.read_text(encoding="utf-8"))

    def test_replace_deployment_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.dll"
            destination = root / "deploy" / "target.dll"
            source.write_bytes(b"new")
            destination.parent.mkdir()
            destination.write_bytes(b"old")

            replace_deployment_artifact(source, destination)

            self.assertEqual(b"new", destination.read_bytes())

    def test_replace_deployment_directory_preserves_symlinks(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.plugin"
            destination = root / "deploy" / "target.plugin"
            source.mkdir()
            (source / "binary").write_bytes(b"plugin")
            try:
                (source / "binary-link").symlink_to("binary")
            except OSError as error:
                if sys.platform == "win32" and error.winerror == 1314:
                    self.skipTest("Windows symbolic links require elevated privilege")
                raise
            destination.mkdir(parents=True)
            (destination / "stale").write_bytes(b"stale")

            replace_deployment_artifact(source, destination)

            self.assertFalse((destination / "stale").exists())
            self.assertTrue((destination / "binary-link").is_symlink())
            self.assertEqual(b"plugin", (destination / "binary-link").read_bytes())


if __name__ == "__main__":
    unittest.main()
