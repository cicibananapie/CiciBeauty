# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

from __future__ import annotations

import sys
import subprocess
import tempfile
import unittest
from pathlib import Path
from itertools import product
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mac import codesign as mac_codesign


class SigningRetryTests(unittest.TestCase):
    def test_bundle_signs_luma_before_outer_bundle(self) -> None:
        bundle = Path("Cici.app")
        with mock.patch.object(mac_codesign, "codesign_file") as sign, mock.patch.object(
            mac_codesign, "verify_codesign"
        ), mock.patch("builtins.print"):
            mac_codesign.codesign_bundle(bundle, "test identity")

        targets = [call.args[0] for call in sign.call_args_list]
        self.assertIn(f"{bundle}/Contents/Helpers/ciciluma", targets)
        self.assertIn(f"{bundle}/Contents/Helpers/cicicrashhandler", targets)
        self.assertEqual(bundle, targets[-1])

    def test_timestamp_failure_is_retried_and_can_succeed(self) -> None:
        for target_name, message in product(
            ("setup.dmg", "helper", "library.dylib", "Setup.app"),
            (
                "A timestamp was expected but was not found.",
                "The timestamp service is not available.",
            ),
        ):
            with self.subTest(target=target_name, message=message), tempfile.TemporaryDirectory() as directory:
                target = Path(directory) / target_name
                target.mkdir() if target.suffix == ".app" else target.touch()
                failure = mac_codesign.subprocess.CalledProcessError(1, "codesign", stderr=message)
                success = mac_codesign.subprocess.CompletedProcess([], 0, stderr="")
                with mock.patch.object(mac_codesign.subprocess, "run", side_effect=[failure, success]) as run_mock, mock.patch.object(mac_codesign.time, "sleep") as sleep_mock:
                    mac_codesign.codesign_file(target, "Developer ID Application: test")

                self.assertEqual(run_mock.call_count, 2)
                self.assertEqual(run_mock.call_args_list[0], run_mock.call_args_list[1])
                self.assertIn("--timestamp", run_mock.call_args.args[0])
                sleep_mock.assert_called_once_with(2)

    def test_failures_stop_at_limit_or_immediately_when_not_retryable(self) -> None:
        for target_name, (message, timestamp, attempts) in product(
            ("setup.dmg", "helper", "library.dylib", "Setup.app"),
            (
                ("A timestamp was expected but was not found.", True, 3),
                ("The specified item could not be found in the keychain.", True, 1),
                ("The timestamp was not valid.", True, 1),
                ("A timestamp was expected but was not found.", False, 1),
            ),
        ):
            with self.subTest(target=target_name, message=message, timestamp=timestamp), tempfile.TemporaryDirectory() as directory:
                target = Path(directory) / target_name
                target.mkdir() if target.suffix == ".app" else target.touch()
                failure = mac_codesign.subprocess.CalledProcessError(1, "codesign", stderr=message)
                with mock.patch.object(mac_codesign.subprocess, "run", side_effect=failure) as run_mock, mock.patch.object(mac_codesign.time, "sleep") as sleep_mock:
                    with self.assertRaises(mac_codesign.subprocess.CalledProcessError) as raised:
                        mac_codesign.codesign_file(target, "Developer ID Application: test", timestamp=timestamp)

                self.assertIs(raised.exception, failure)
                self.assertEqual(run_mock.call_count, attempts)
                self.assertEqual(sleep_mock.call_args_list, [mock.call(2)] * (attempts - 1))


    def test_developer_id_signing_keeps_runtime_entitlements_and_timestamp(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "cicibeauty.so"
            binary.write_bytes(b"fixture")
            with mock.patch.object(mac_codesign.subprocess, "run") as run_mock:
                mac_codesign.codesign_file(binary, "Developer ID Application: test")

        command = run_mock.call_args.args[0]
        self.assertIn("--options", command)
        self.assertIn("runtime", command)
        self.assertIn("--entitlements", command)
        self.assertIn("--timestamp", command)

    def test_ad_hoc_signing_omits_runtime_and_timestamp(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "cicibeauty.so"
            binary.write_bytes(b"fixture")
            with mock.patch.object(mac_codesign.subprocess, "run") as run_mock:
                mac_codesign.codesign_file(
                    binary,
                    "-",
                    hardened_runtime=False,
                    timestamp=False,
                )

        command = run_mock.call_args.args[0]
        self.assertNotIn("--options", command)
        self.assertNotIn("--entitlements", command)
        self.assertNotIn("--timestamp", command)



if __name__ == "__main__":
    unittest.main()
