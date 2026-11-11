# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import symbols_utils as symbols


class SymbolArchiveTests(unittest.TestCase):
    def test_windows_drive_selection_excludes_non_fixed_drives(self):
        with mock.patch.object(symbols.ctypes, "windll", create=True) as windll:
            windll.kernel32.GetDriveTypeW.side_effect = lambda drive: {
                "C:\\": 3, "E:\\": 2, "F:\\": 4, "G:\\": 5, "H:\\": 3,
            }.get(drive, 1)
            self.assertEqual([Path("C:/"), Path("H:/")], symbols._windows_fixed_drives())

    def test_windows_symbol_root_uses_available_drive_with_most_free_space(self):
        with tempfile.TemporaryDirectory() as directory:
            drives = [Path(directory) / name for name in ("system", "data")]
            for drive in drives:
                drive.mkdir()
            for available, expected in ((drives[:1], drives[0]), (drives, drives[1])):
                with self.subTest(available=available), mock.patch.object(
                    symbols, "_windows_fixed_drives", return_value=available
                ), mock.patch.object(
                    symbols.shutil, "disk_usage",
                    side_effect=lambda drive: mock.Mock(free=100 if drive == drives[0] else 200),
                ):
                    root = symbols._windows_symbol_storage_root()
                self.assertEqual(expected / "CiciSymbols", root)
                self.assertTrue(root.is_dir())
                self.assertEqual([], list(root.iterdir()))

    def test_windows_symbol_root_skips_inaccessible_and_unwritable_drives(self):
        with tempfile.TemporaryDirectory() as directory:
            missing, blocked, writable = [
                Path(directory) / name for name in ("missing", "blocked", "writable")
            ]
            blocked.mkdir()
            writable.mkdir()
            # A file at the target path prevents creating a symbol directory.
            (blocked / "CiciSymbols").write_bytes(b"existing file")
            with mock.patch.object(
                symbols, "_windows_fixed_drives", return_value=[missing, blocked, writable]
            ), mock.patch.object(
                symbols.shutil, "disk_usage",
                side_effect=[OSError("unavailable"), mock.Mock(free=200), mock.Mock(free=100)],
            ):
                self.assertEqual(writable / "CiciSymbols", symbols._windows_symbol_storage_root())
            self.assertEqual(b"existing file", (blocked / "CiciSymbols").read_bytes())

    def test_windows_symbol_root_reports_when_no_drive_is_usable(self):
        with mock.patch.object(symbols, "_windows_fixed_drives", return_value=[]):
            with self.assertRaisesRegex(OSError, "Set CICI_SYMBOL_ROOT"):
                symbols._windows_symbol_storage_root()

    def test_configured_symbol_root_bypasses_drive_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.dict(symbols.os.environ, {"CICI_SYMBOL_ROOT": directory}):
                with mock.patch.object(symbols, "_windows_symbol_storage_root") as select:
                    self.assertEqual(Path(directory).resolve(), symbols.default_symbol_storage_root())
            select.assert_not_called()

    def test_archive_preserves_files_hashes_and_build_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "build/symbols"
            source.mkdir(parents=True)
            pdb = source / "plugin.pdb"
            pdb.write_bytes(b"matching-symbol-data")
            permanent = root / "permanent/Release"
            metadata = root / "runtime-manifest.json"
            metadata.write_text('{"build_id":"release-1"}')
            with mock.patch.object(symbols, "current_platform_kind", return_value="windows"):
                symbols.package_symbols_sidecar(
                    root / "build", "Release", root / "symbols.zip", symbol_root=source,
                    archive_dir=permanent, metadata_files=[metadata],
                    build_metadata={"build_id": "release-1"})
            with zipfile.ZipFile(root / "symbols.zip") as archive:
                manifest = json.loads(archive.read("symbol-manifest.json"))
                self.assertEqual(pdb.read_bytes(), archive.read("symbols/plugin.pdb"))
                self.assertIn("runtime-manifest.json", archive.namelist())
            self.assertEqual("release-1", manifest["build"]["build_id"])
            self.assertEqual(hashlib.sha256(pdb.read_bytes()).hexdigest(), manifest["files"][0]["sha256"])
            self.assertEqual(pdb.stat().st_size, manifest["files"][0]["size"])
            self.assertEqual(pdb.read_bytes(), (permanent / pdb.name).read_bytes())
            self.assertEqual(manifest, json.loads((permanent / "symbol-manifest.json").read_text()))

    def test_missing_release_symbols_and_name_collisions_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(FileNotFoundError):
                symbols.package_symbols_sidecar(root, "Release", root / "symbols.zip", symbol_root=root)
            a, b = root / "a/plugin.pdb", root / "b/plugin.pdb"
            for path in (a, b):
                path.parent.mkdir()
                path.write_bytes(b"symbols")
            with mock.patch.object(symbols, "current_platform_kind", return_value="windows"):
                with self.assertRaises(ValueError):
                    symbols.package_symbols_sidecar(root, "Release", root / "symbols.zip",
                                                    symbol_root=a.parent, extra_symbol_items=[b])

    def test_windows_formal_pdb_paths_exclude_user_profiles(self):
        symbols.validate_pdb_output_root(Path("D:/CiciSymbols/version/build"))
        for path in ("C:/Users/alice/symbols", "symbols", "D:/Documents and Settings/alice"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                symbols.validate_pdb_output_root(Path(path))

    def test_wrong_binary_uuid_is_rejected(self):
        with mock.patch.object(symbols, "dwarf_uuids", side_effect=[
            {("SYMBOL", "arm64")}, {("BINARY", "arm64")}
        ]):
            with self.assertRaises(ValueError):
                symbols.validate_symbol_pairs([Path("helper.dSYM")], [Path("helper")], "macos")

    def test_windows_uses_codeview_path_even_when_output_name_differs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            pdb, binary = root / "ciciuninstall.pdb", root / "uninstall.exe"
            pdb.write_bytes(b"symbols")
            binary.write_bytes(b"PE fixture RSDS" + bytes(20) + str(pdb).encode() + b"\0")
            pairs = symbols.validate_symbol_pairs([pdb], [binary], "windows")
            self.assertEqual("ciciuninstall.pdb", pairs[0]["symbol"])
            with self.assertRaises(ValueError):
                symbols.validate_symbol_pairs([root / "other.pdb"], [binary], "windows")

    @unittest.skipUnless(sys.platform == "darwin", "requires Apple compiler and dSYM tools")
    def test_native_dsym_matches_stripped_binary_and_maps_source_paths(self):
        with tempfile.TemporaryDirectory(prefix="cici source paths ") as directory:
            root = Path(directory).resolve()
            source = root / "example.cpp"
            source.write_text("int main() { return 0; }\n")
            binary = root / "example"
            object_file = root / "example.o"
            subprocess.run(["xcrun", "clang++", "-g",
                            f"-fdebug-prefix-map={root}=/CiciSources/test",
                            "-fdebug-compilation-dir=/CiciBuild/test",
                            "-c", str(source), "-o", str(object_file)], check=True)
            subprocess.run(["xcrun", "clang++", str(object_file), "-o", str(binary)], check=True)
            dsym = root / "example.dSYM"
            subprocess.run(["xcrun", "dsymutil", str(binary), "-o", str(dsym)], check=True)
            subprocess.run(["xcrun", "strip", "-S", str(binary)], check=True)
            pairs = symbols.validate_symbol_pairs([dsym], [binary], "macos")
            self.assertEqual("example.dSYM", pairs[0]["symbol"])
            debug = subprocess.run(["xcrun", "dwarfdump", "--debug-info", str(dsym)],
                                   check=True, capture_output=True, text=True).stdout
            self.assertNotIn(str(root), debug.partition(".debug_info contents:")[2])
            self.assertIn("/CiciSources/test/example.cpp", debug)


if __name__ == "__main__":
    unittest.main()
