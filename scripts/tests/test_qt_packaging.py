# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


class QtPlatformPackagingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        import package
        cls.packager = package

    def create_platform_fixture(self, root: Path) -> Path:
        platforms = root / "source" / "platforms"
        platforms.mkdir(parents=True)
        for name in (
            "qdirect2d.dll",
            "qdirect2dd.dll",
            "qdirect2dd.pdb",
            "qminimal.dll",
            "qminimald.dll",
            "qminimald.pdb",
            "qwindows.dll",
            "qwindowsd.dll",
            "qwindowsd.pdb",
            "metadata.json",
        ):
            (platforms / name).write_bytes(b"fixture")
        return platforms

    def test_release_excludes_qt_debug_plugins_and_symbols(self) -> None:
        with tempfile.TemporaryDirectory() as directory, mock.patch("builtins.print"):
            root = Path(directory)
            platforms = self.create_platform_fixture(root)
            target = root / "target"

            self.packager.copy_qt_platform_plugins(platforms, target, "Release")

            copied = {item.name for item in (target / "platforms").iterdir()}

        self.assertEqual(
            {"qdirect2d.dll", "qminimal.dll", "qwindows.dll", "metadata.json"},
            copied,
        )

    def test_debug_keeps_debug_plugins_but_excludes_symbols(self) -> None:
        with tempfile.TemporaryDirectory() as directory, mock.patch("builtins.print"):
            root = Path(directory)
            platforms = self.create_platform_fixture(root)
            target = root / "target"

            self.packager.copy_qt_platform_plugins(platforms, target, "Debug")

            copied = {item.name for item in (target / "platforms").iterdir()}

        self.assertEqual(
            {
                "qdirect2d.dll",
                "qdirect2dd.dll",
                "qminimal.dll",
                "qminimald.dll",
                "qwindows.dll",
                "qwindowsd.dll",
                "metadata.json",
            },
            copied,
        )



if __name__ == "__main__":
    unittest.main()
