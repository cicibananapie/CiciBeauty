# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

from __future__ import annotations

import io
import json
import struct
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from package import assemble, write_windows_setup
from runtime_package import validate_runtime


class RuntimePackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cici-package-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.runtime = self.root / "runtime"
        self.runtime.mkdir()
        self.manifest = {"schema_version": 1, "platform": "windows", "architecture": "x64",
                         "version": "1.2.3", "configuration": "Debug",
                         "official_build": False}
        (self.runtime / "runtime-manifest.json").write_text(json.dumps(self.manifest))
        for name in ("installer/ciciinstaller.exe", "root/cicirunner.exe", "root/uninstall.exe",
                     "slot/ciciluma.dll", "slot/ciciupdater.dll",
                     "slot/cicifeedback.dll", "slot/cicirelay.dll", "slot/cicicrashreporter.dll",
                     "slot/cicicrashhandler.dll", "slot/ciciplayer.dll", "slot/Resources/defaultsetting.json",
                     "slot/Resources/effects/1.cici", "slot/Resources/models/model.bin"):
            file = self.runtime / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(b"private-runtime")

    def validate(self, **kwargs):
        return validate_runtime(self.runtime, **dict(
            {"platform": "windows", "version": "1.2.3"}, **kwargs))

    def test_matching_runtime_and_missing_helper(self):
        self.assertEqual(self.validate(), self.manifest)
        (self.runtime / "slot/ciciluma.dll").unlink()
        with self.assertRaises(FileNotFoundError):
            self.validate()

    def test_mismatch_and_bundled_plugin_are_rejected(self):
        for field, value in (("version", "1.2.4"), ("platform", "macos")):
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.validate(official=True, **{field: value})
        (self.runtime / "slot/cicibeauty.dll").write_bytes(b"stale-plugin")
        with self.assertRaises(ValueError):
            self.validate()

    def test_release_official_runtime_accepts_debug_nonofficial_plugin(self):
        self.manifest.update(configuration="Release", official_build=True)
        (self.runtime / "runtime-manifest.json").write_text(json.dumps(self.manifest))
        self.assertEqual(self.manifest, self.validate(official=False))

    def test_development_plugin_version_can_differ_from_runtime(self):
        self.assertEqual(self.manifest, self.validate(version="9.8.7", official=False))

    def test_malformed_runtime_metadata_is_rejected(self):
        for field, value in (("configuration", "invalid"), ("official_build", "false"),
                             ("architecture", "arm64"), ("version", "latest")):
            with self.subTest(field=field):
                manifest = dict(self.manifest, **{field: value})
                (self.runtime / "runtime-manifest.json").write_text(json.dumps(manifest))
                with self.assertRaises(ValueError):
                    self.validate()

    def test_setup_trailer_matches_existing_installer(self):
        stub, payload, result = (self.root / name for name in ("stub.exe", "payload.zip", "setup.exe"))
        stub.write_bytes(b"installer-stub")
        payload.write_bytes(b"zip-payload")
        write_windows_setup(stub, payload, result)
        data = result.read_bytes()
        magic, version, offset, size = struct.unpack("<24sIQQ", data[-44:])
        self.assertEqual((magic.rstrip(b"\0"), version), (b"CICI_SETUP_PAYLOAD_V1", 1))
        self.assertEqual(data[offset:offset + size], payload.read_bytes())
        self.assertEqual(data[:offset], stub.read_bytes())

    def test_unconfigured_release_fails_before_download(self):
        import package
        config = self.root / "runtime.json"
        config.write_text(json.dumps({"artifacts": {"runtime": {"repository": ""}}}))
        args = SimpleNamespace(output_dir=self.root / "output", runtime_package=None,
                               runtime_config=config, version="1.2.3", channel_id="test")
        with mock.patch.object(package, "install_dependencies") as install:
            with self.assertRaisesRegex(ValueError, "Runtime Release is not configured"):
                assemble(args, self.root / "binaries")
        install.assert_not_called()

    def test_release_assembly_uses_rebuilt_plugin_without_private_checks(self):
        self.manifest["configuration"] = "Release"
        (self.runtime / "runtime-manifest.json").write_text(json.dumps(self.manifest))
        archive = self.root / "runtime.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(self.runtime, arcname="runtime")
        binaries = self.root / "binaries"
        binaries.mkdir()
        for name in ("cicibeauty.dll", "cicibeautyStub.dll"):
            (binaries / name).write_bytes(b"rebuilt-public-plugin")
        args = SimpleNamespace(output_dir=self.root / "output", runtime_package=archive,
                               version="1.2.3", config="Release", channel_id="test", official=False,
                               build_dir=self.root / "build", stage_dir=None, build_id="test-build")
        dependencies = self.root / "dependencies"
        (dependencies / "vcrt").mkdir(parents=True)
        (dependencies / "vcrt/VC_redist.x64.exe").write_bytes(b"public-vcrt")
        (dependencies / "obsQT/plugins/platforms").mkdir(parents=True)
        (dependencies / "obsQT/plugins/platforms/qwindows.dll").write_bytes(b"public-qt")
        args.build_dir.mkdir()
        (args.build_dir / "CMakeCache.txt").write_text(f"CICI_DEPENDENCIES_DIR:PATH={dependencies}\n")
        with mock.patch("package.sys.platform", "win32"), mock.patch("package.package_symbols_sidecar"):
            products = assemble(args, binaries)
        with zipfile.ZipFile(products[0]) as update:
            self.assertEqual(update.read("cicibeauty.dll"), b"rebuilt-public-plugin")
            self.assertEqual(update.read("ciciluma.dll"), b"private-runtime")
            self.assertEqual(update.read("platforms/qwindows.dll"), b"public-qt")
            self.assertNotIn("slot/cicibeauty.dll", update.namelist())
            self.assertIn("Resources/LICENSES/GPL-2.0-or-later.txt", update.namelist())
        setup = products[1].read_bytes()
        _, _, offset, size = struct.unpack("<24sIQQ", setup[-44:])
        with zipfile.ZipFile(io.BytesIO(setup[offset:offset + size])) as payload:
            for file in ("slot/cicibeauty.dll", "root/cicirunner.exe", "root/uninstall.exe", "redist/VC_redist.x64.exe"):
                self.assertIn(file, payload.namelist())
            self.assertEqual(payload.read("redist/VC_redist.x64.exe"), b"public-vcrt")

    def test_local_macos_assembly_uses_and_signs_the_existing_development_app(self):
        bundle_root = self.root / "private build/artifacts/bin"
        app = bundle_root / "Debug/ciciinstaller.app"
        contents = app / "Contents"
        for name in ("Frameworks/cicibeauty.so", "Helpers/cicibeautyStub.plugin/Contents/MacOS/cicibeautyStub",
                     "Frameworks/libcrypto.3.dylib"):
            file = contents / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(b"current-local-build")
        library = contents / "Frameworks/libcrypto.3.dylib"
        library.chmod(0o444)
        args = SimpleNamespace(output_dir=self.root / "output", runtime_root=self.runtime,
                               local_bundle_root=bundle_root, version="1.2.3", config="Debug",
                               channel_id="test", official=False, build_dir=self.root / "build",
                               stage_dir=app.parent, build_id="local-test", sign_identity="")

        def collect_symbols(_build, _config, _archive, **options):
            self.assertEqual(b"current-local-build", options["binary_files"][0].read_bytes())

        sign = mock.Mock()
        modules = {
            "codesign": SimpleNamespace(codesign_bundle=sign, codesign_file=mock.Mock(),
                                        verify_codesign=lambda _: True),
            "dittozip": SimpleNamespace(create_update_zip=lambda *_: True),
            "dmg": SimpleNamespace(create_direct_run_dmg=mock.Mock()),
        }
        with mock.patch("package.sys.platform", "darwin"), mock.patch(
            "package.validate_runtime", return_value=self.manifest
        ), mock.patch("package.package_symbols_sidecar", side_effect=collect_symbols), mock.patch.dict(
            sys.modules, modules
        ):
            products = assemble(args, self.root / "unused-flat-binaries")
        sign.assert_called_once_with(str(app), "-", hardened_runtime=False, timestamp=False)
        self.assertEqual(2, len(products))
        self.assertEqual(b"current-local-build", library.read_bytes())
        self.assertEqual(0o444, library.stat().st_mode & 0o777)
        self.assertTrue((contents / "Resources/cicilogo.png").is_file())


if __name__ == "__main__":
    unittest.main()
