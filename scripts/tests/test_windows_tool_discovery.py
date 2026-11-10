# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


class SigningToolDiscoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = Path(__file__).resolve().parents[1] / "windows/codesign.py"
        spec = importlib.util.spec_from_file_location("windows_codesign_discovery_test", source)
        cls.signing = importlib.util.module_from_spec(spec)
        # These modules are used by signing/UI, not by tool discovery.
        with mock.patch.dict(sys.modules, {name: mock.Mock() for name in (
            "keyring", "win32con", "win32gui",
        )}):
            spec.loader.exec_module(cls.signing)

    def test_explicit_path_and_invalid_override(self):
        with tempfile.TemporaryDirectory() as directory:
            tool = Path(directory) / "signtool.exe"
            tool.touch()
            with mock.patch.dict("os.environ", {"CICI_SIGNTOOL_PATH": str(tool)}, clear=True):
                self.assertEqual(str(tool), self.signing.get_signtool_path())
                tool.unlink()
                with mock.patch.object(self.signing.shutil, "which") as search:
                    with self.assertRaises(FileNotFoundError):
                        self.signing.get_signtool_path()
                    search.assert_not_called()

    def test_path_takes_precedence_over_sdk_search(self):
        with mock.patch.dict("os.environ", {}, clear=True), mock.patch.object(
            self.signing.shutil, "which", return_value="sdk-tool"
        ):
            self.assertEqual("sdk-tool", self.signing.get_signtool_path())

    def test_custom_sdk_uses_numeric_version_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for version in ("10.0.9.0", "10.0.10.0"):
                tool = root / "bin" / version / "x64/signtool.exe"
                tool.parent.mkdir(parents=True)
                tool.touch()
            with (
                mock.patch.dict("os.environ", {"WindowsSdkDir": str(root)}, clear=True),
                mock.patch.object(self.signing.shutil, "which", return_value=None),
                mock.patch.dict(sys.modules, {"winreg": None}),
            ):
                self.assertEqual(str(tool), self.signing.get_signtool_path())

    def test_registry_finds_sdk_outside_program_files(self):
        with tempfile.TemporaryDirectory() as directory:
            tool = Path(directory) / "bin/x64/signtool.exe"
            tool.parent.mkdir(parents=True)
            tool.touch()
            registry = mock.MagicMock()
            registry.QueryValueEx.side_effect = [(directory, 1), OSError("not installed")]
            with (
                mock.patch.dict("os.environ", {}, clear=True),
                mock.patch.object(self.signing.shutil, "which", return_value=None),
                mock.patch.dict(sys.modules, {"winreg": registry}),
            ):
                self.assertEqual(str(tool), self.signing.get_signtool_path())


if __name__ == "__main__":
    unittest.main()
