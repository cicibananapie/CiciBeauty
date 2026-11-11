# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

from __future__ import annotations

import sys
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import cici


class BuildWorkflowTests(unittest.TestCase):
    def setUp(self):
        platform = mock.patch("cici.sys.platform", "darwin")
        platform.start()
        self.addCleanup(platform.stop)
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.runtime = Path(temporary.name)
        (self.runtime / "runtime-manifest.json").write_text(json.dumps({
            "schema_version": 1, "platform": "macos", "architecture": "arm64",
            "version": "1.2.3", "configuration": "Release", "official_build": True,
        }))
        contents = self.runtime / "ciciinstaller.app/Contents"
        for name in ("MacOS/ciciinstaller", "Helpers/ciciluma", "Helpers/ciciupdater",
                     "Helpers/cicifeedback", "Helpers/cicicrashreporter", "Helpers/cicicrashhandler",
                     "Helpers/cicirelay.app/Contents/MacOS/cicirelay", "Resources/defaultsetting.json",
                     "Resources/effects/1.cici", "Resources/models/model.bin"):
            path = contents / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"runtime")
        patcher = mock.patch("package.prepare_runtime", return_value=self.runtime)
        self.prepare = patcher.start()
        self.addCleanup(patcher.stop)

    def test_development_build_assembles_before_deployment(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            operations = []

            def assemble(args, binaries):
                operations.append("assemble")
                self.assertEqual(root / "build/artifacts/bin/Debug", args.stage_dir)
                self.assertFalse(args.official)

            with mock.patch("cici.sys.platform", "darwin"), mock.patch(
                "pull_depends.process_deps", side_effect=lambda _: operations.append("dependencies")
            ), mock.patch(
                "cici.build", side_effect=lambda _: operations.append("plugin") or root / "binaries"
            ), mock.patch("package.assemble", side_effect=assemble), mock.patch(
                "cici.deploy_local", side_effect=lambda _: operations.append("deploy")
            ):
                cici.main(["dev", "--pull-deps",
                           "--build-dir", str(root / "build"), "--output-dir", str(root / "output")])
            self.assertEqual(["dependencies", "plugin", "assemble", "deploy"], operations)

    def test_binary_runtime_builds_and_assembles_without_deployment(self):
        with mock.patch("cici.sys.platform", "darwin"), mock.patch("cici.build") as build, mock.patch(
            "package.assemble"
        ) as assemble, mock.patch("cici.deploy_local") as deploy:
            cici.main(["package"])
        build.assert_called_once()
        assemble.assert_called_once()
        deploy.assert_not_called()
        self.assertEqual("1.2.3", build.call_args.args[0].version)
        self.assertEqual(self.runtime, assemble.call_args.args[0].runtime_root)
        self.prepare.assert_called_once()

    def test_explicit_development_version_and_plugin_channel_are_preserved(self):
        with mock.patch("cici.build") as build, mock.patch("package.assemble"):
            cici.main(["package", "--version", "9.8.7", "--channel-id", "custom"])
        args = build.call_args.args[0]
        self.assertEqual("9.8.7", args.version)
        self.assertEqual("custom", args.channel_id)

    def test_official_version_mismatch_fails_before_plugin_compilation(self):
        with mock.patch("cici.build") as build:
            with self.assertRaisesRegex(ValueError, "Runtime version mismatch"):
                cici.main(["package", "--version", "9.8.7", "--official"])
        build.assert_not_called()

    def test_invalid_runtime_fails_before_plugin_compilation(self):
        (self.runtime / "ciciinstaller.app/Contents/Helpers/ciciluma").unlink()
        with mock.patch("cici.build") as build:
            with self.assertRaises(FileNotFoundError):
                cici.main(["package"])
        build.assert_not_called()

    def test_official_source_requires_matching_tag_and_clean_worktree(self):
        for head, status, succeeds in (("commit", "", True), ("other", "", False),
                                       ("commit", " M source.cpp", False)):
            with self.subTest(head=head, status=status), mock.patch(
                "cici.subprocess.run",
                side_effect=[mock.Mock(stdout="commit"), mock.Mock(stdout=head), mock.Mock(stdout=status)],
            ) as git:
                if succeeds:
                    cici.validate_official_source("1.2.3")
                else:
                    with self.assertRaises(ValueError):
                        cici.validate_official_source("1.2.3")
                self.assertIn("refs/tags/macos_1.2.3^{commit}", git.call_args_list[0].args[0])

    def test_missing_official_tag_is_rejected(self):
        import subprocess
        with mock.patch("cici.subprocess.run", side_effect=subprocess.CalledProcessError(1, "git")):
            with self.assertRaisesRegex(ValueError, "does not exist"):
                cici.validate_official_source("1.2.3")

    def test_local_runtime_flags_are_not_public(self):
        with mock.patch("sys.stderr"):
            for flag in ("--runtime-package", "--runtime-source"):
                with self.subTest(flag=flag), self.assertRaises(SystemExit):
                    cici.create_parser().parse_args(["package", flag, "runtime"])

    def test_platform_release_config_is_selected_by_default(self):
        for platform, filename in (("darwin", "runtime_mac.json"), ("win32", "runtime_win.json")):
            with self.subTest(platform=platform), mock.patch("cici.sys.platform", platform):
                args = cici.create_parser().parse_args(["dev"])
                self.assertEqual(cici.ROOT / "config" / filename, args.runtime_config)
                self.assertIsNone(args.runtime_package)

    def test_plugin_build_selects_only_public_targets(self):
        args = cici.create_parser().parse_args(["build", "--build-dir", "build-test"])
        with mock.patch("cici.subprocess.run") as run:
            cici.build(args)
        command = run.call_args.args[0]
        self.assertEqual(["cicibeauty", "cicibeautyStub"], command[command.index("--target") + 1:])

    def test_official_build_preserves_existing_build_tree(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            marker = root / "existing-build"
            marker.write_text("keep")
            with mock.patch("cici.sys.platform", "darwin"), mock.patch("cici.build") as build:
                with self.assertRaisesRegex(ValueError, "empty --build-dir"):
                    cici.main(["build", "--official", "--build-dir", str(root)])
            build.assert_not_called()
            self.assertEqual("keep", marker.read_text())

    def test_ide_opens_plugin_project_without_assembly_or_deployment(self):
        with mock.patch("cici.sys.platform", "darwin"), mock.patch(
            "cici.build"
        ), mock.patch("cici.subprocess.run") as run, mock.patch("package.assemble") as assemble, mock.patch(
            "cici.deploy_local"
        ) as deploy:
            cici.main(["dev", "--ide"])
        run.assert_called_once_with(["open", str(cici.ROOT / "build/cicibeauty.xcodeproj")], check=True)
        assemble.assert_not_called()
        deploy.assert_not_called()

    def test_windows_build_uses_selected_visual_studio_environment(self):
        args = cici.create_parser().parse_args(["build"])
        with mock.patch("cici.sys.platform", "win32"), mock.patch(
            "cici.find_vs_environment", return_value=Path("custom/VsDevCmd.bat")
        ), mock.patch("cici.run_streaming_shell") as run:
            cici.build(args)
        command = run.call_args.args[0]
        self.assertIn('"custom/VsDevCmd.bat" && ', command)
        self.assertIn("--target cicibeauty cicibeautyStub", command)


if __name__ == "__main__":
    unittest.main()
