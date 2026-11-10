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
