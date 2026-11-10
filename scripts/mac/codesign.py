# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

import subprocess
import os
import sys
import time
from pathlib import Path

def verify_codesign(plugin_path):
    # Build the command list.
    command = [
        "codesign",
        "--verify",
        "--deep",
        "--strict",
        "--verbose=2",
        plugin_path
    ]

    try:
        # text=True decodes byte streams into strings automatically.
        # capture_output=True captures both stdout and stderr.
        result = subprocess.run(command, capture_output=True, text=True, check=False)

        # Check the return code. 0 means the signature is valid.
        if result.returncode == 0:
            # codesign --verbose usually writes progress to stderr; on success this is normal logging.
            if result.stderr:
                print("codesign verbose:")
                print(result.stderr)
            if result.stdout:
                print(result.stdout)
            print(f"✅ Verification passed: {plugin_path} signature is valid.")
            return True
        else:
            if result.stderr:
                print("codesign error output:")
                print(result.stderr)
            if result.stdout:
                print(result.stdout)
            print(f"❌ Verification failed: exit code {result.returncode}")
            return False

    except FileNotFoundError:
        print("Error: 'codesign' command not found. Please install Xcode Command Line Tools.")
        return False

def _run_codesign(command, *, timestamp):
    for attempt in range(1, 4):
        try:
            result = subprocess.run(command, check=True, stderr=subprocess.PIPE, text=True)
        except subprocess.CalledProcessError as error:
            stderr = error.stderr or ""
            print(stderr, file=sys.stderr, end="")
            temporary_timestamp_error = (
                "A timestamp was expected but was not found." in stderr
                or "The timestamp service is not available." in stderr
            )
            if not timestamp or not temporary_timestamp_error or attempt == 3:
                raise
            print(f"Timestamp request failed; retrying signing of {command[-1]} ({attempt + 1}/3) in 2 seconds...", flush=True)
            time.sleep(2)
        else:
            if result.stderr:
                print(result.stderr, file=sys.stderr, end="")
            return


def codesign_file(file, identity, *, hardened_runtime=True, timestamp=True):
    current_dir = Path(__file__).resolve().parent
    entitlements = f"{current_dir}/entitlements.plist"
    path = Path(file)

    if not path.exists():
        raise FileNotFoundError(f"Missing signing target: {file}")

    if path.is_file():
        if path.suffix.lower() == ".dmg":
            print(f"📀 Signing disk image: {path.name}")
            command = ["codesign", "--force"]
            if timestamp:
                command.append("--timestamp")
            command.extend(("--sign", identity, str(path)))
            _run_codesign(command, timestamp=timestamp)
            return

        action = "Hardening" if hardened_runtime else "Ad-hoc signing"
        print(f"✒️  {action} binary: {path.name}")
        # No --deep here, it's a flat Mach-O.
        command = ["codesign", "--force"]
        if hardened_runtime:
            command.extend(("--options", "runtime", "--entitlements", entitlements))
        if timestamp:
            command.append("--timestamp")
        command.extend(("--sign", identity, str(path)))
        _run_codesign(command, timestamp=timestamp)
        return

    if path.is_dir():
        print(f"📦 Sealing bundle: {path.name}")
        command = ["codesign", "--force"]
        if hardened_runtime:
            command.extend(("--options", "runtime"))
        if timestamp:
            command.append("--timestamp")
        command.extend(("--sign", identity, str(path)))
        _run_codesign(command, timestamp=timestamp)
        return

def codesign_bundle(bundle, identity, *, hardened_runtime=True, timestamp=True):
    file_list = [
        f"{bundle}/Contents/MacOS/ciciinstaller",
        f"{bundle}/Contents/Helpers/cicibeautyStub.plugin/Contents/MacOS/cicibeautyStub",
        f"{bundle}/Contents/Helpers/cicibeautyStub.plugin",
        f"{bundle}/Contents/Helpers/cicirelay.app/Contents/MacOS/cicirelay",
        f"{bundle}/Contents/Helpers/cicirelay.app",
        f"{bundle}/Contents/Helpers/cicifeedback",
        f"{bundle}/Contents/Helpers/cicicrashreporter",
        f"{bundle}/Contents/Helpers/cicicrashhandler",
        f"{bundle}/Contents/Helpers/ciciupdater",
        f"{bundle}/Contents/Helpers/ciciluma",
        #f"{bundle}/Contents/Frameworks/libinner.dylib",
        #f"{bundle}/Contents/Frameworks/libmain.dylib",
        f"{bundle}/Contents/Frameworks/libomp.dylib",
        f"{bundle}/Contents/Frameworks/libjpeg.8.dylib",
        f"{bundle}/Contents/Frameworks/libcrypto.3.dylib",
        f"{bundle}/Contents/Frameworks/libcurl.4.dylib",
        f"{bundle}/Contents/Frameworks/cicibeauty.so",
        bundle
    ]

    
    for file in file_list:
        codesign_file(
            file,
            identity,
            hardened_runtime=hardened_runtime,
            timestamp=timestamp,
        )
    print("All components have been signed and verified.")
    verify_codesign(bundle)
