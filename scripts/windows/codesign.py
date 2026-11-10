# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

import argparse
import os
import re
import shutil
import subprocess
import threading
import time
from pathlib import Path

import keyring
import win32con
import win32gui


SERVICE_NAME = "CiciSigningService"
ACCOUNT_NAME = "CiciSign_user"
DEFAULT_TIMESTAMP_URLS = [
    "http://timestamp.digicert.com",
    "http://timestamp.sectigo.com",
    "http://timestamp.globalsign.com/tsa/r6advanced1",
]
DEFAULT_SIGN_RETRIES = 2
SIGN_RETRY_DELAY_SECONDS = 3


def monitor_and_fill_pin(target_title, pin_code):
    """Background monitor: Finds the specific ENABLED and EMPTY Edit control."""
    start_time = time.time()
    while time.time() - start_time < 60:
        hwnd = win32gui.FindWindow(None, target_title)
        if hwnd:
            child_handles = []

            def callback(child_hwnd, _):
                if win32gui.GetClassName(child_hwnd) == "Edit":
                    is_enabled = win32gui.IsWindowEnabled(child_hwnd)
                    text = win32gui.GetWindowText(child_hwnd)
                    if is_enabled and not text:
                        child_handles.append(child_hwnd)
                return True

            win32gui.EnumChildWindows(hwnd, callback, None)

            if child_handles:
                target_edit = child_handles[0]
                win32gui.SendMessage(target_edit, win32con.WM_SETTEXT, 0, pin_code)
                win32gui.PostMessage(hwnd, win32con.WM_KEYDOWN, win32con.VK_RETURN, 0)
                print(f"[Monitor] Found target Edit and filled PIN in '{target_title}'.")
                break

        time.sleep(0.5)


def get_signtool_path():
    """Use an explicit tool, PATH, or an installed Windows SDK."""
    configured = os.environ.get("CICI_SIGNTOOL_PATH")
    if configured:
        if not Path(configured).is_file():
            raise FileNotFoundError(f"CICI_SIGNTOOL_PATH does not exist: {configured}")
        return configured
    discovered = shutil.which("signtool.exe") or shutil.which("signtool")
    if discovered:
        return discovered

    roots = []
    sdk_directory = os.environ.get("WindowsSdkDir")
    if sdk_directory:
        roots.append(Path(sdk_directory))
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                            r"SOFTWARE\Microsoft\Windows Kits\Installed Roots",
                            0, winreg.KEY_READ | winreg.KEY_WOW64_32KEY) as key:
            for name in ("KitsRoot10", "KitsRoot81"):
                try:
                    roots.append(Path(winreg.QueryValueEx(key, name)[0]))
                except OSError:
                    pass
    except (ImportError, OSError):
        pass
    program_files = os.environ.get("ProgramFiles(x86)")
    if program_files:
        roots.extend(Path(program_files) / "Windows Kits" / version for version in ("10", "8.1"))
    for root in roots:
        matches = [path for path in (root / "bin").glob("*/x64/signtool.exe") if path.is_file()]
        if matches:
            return str(max(matches, key=lambda path: tuple(int(value) for value in re.findall(r"\d+", path.parent.parent.name))))
        candidate = root / "bin/x64/signtool.exe"
        if candidate.is_file():
            return str(candidate)
    return None


def is_signed(file_path):
    signtool = get_signtool_path()
    if not signtool:
        print("Error: signtool.exe not found. Please install Windows SDK.")
        return False

    cmd = [signtool, "verify", "/pa", str(file_path)]
    try:
        subprocess.check_output(cmd, stderr=subprocess.STDOUT)
        return True
    except subprocess.CalledProcessError:
        return False


def resolve_timestamp_urls(timestamp_url):
    if timestamp_url is None:
        return DEFAULT_TIMESTAMP_URLS
    if isinstance(timestamp_url, (list, tuple)):
        return [url for url in timestamp_url if url]
    return [timestamp_url]


def require_signtool():
    signtool = get_signtool_path()
    if not signtool:
        raise RuntimeError("signtool.exe not found. Please install Windows SDK.")
    return signtool


def require_sign_password():
    password = keyring.get_password(SERVICE_NAME, ACCOUNT_NAME)
    if not password:
        raise RuntimeError(
            f"No password found in Credential Manager for {SERVICE_NAME}. "
            "Hint: Run keyring.set_password('CiciSigningService', 'CiciSign_user', 'YOUR_PASS') first."
        )
    return password


def require_existing_files(file_list):
    files = [str(f) for f in file_list]
    missing_files = [f for f in files if not os.path.exists(f)]
    if missing_files:
        raise FileNotFoundError("Files to sign do not exist: {}".format(", ".join(missing_files)))
    return files


def unsigned_files(file_list):
    return [f for f in file_list if not is_signed(f)]


def __sign_single_file(signtool_path, file_list, password, timestamp_url):
    valid_files = [str(f) for f in file_list if os.path.exists(f)]
    if not valid_files:
        return False, "Error: No valid files found to sign."

    cmd = [
        signtool_path,
        "sign",
        "/sha1",
        "2233fe259625fc6c75a586d3a9c667e85ee58d83",
        "/fd",
        "sha256",
        "/tr",
        timestamp_url,
        "/td",
        "sha256",
        "/v",
    ]
    cmd.extend(valid_files)

    monitor_thread = threading.Thread(
        target=monitor_and_fill_pin,
        args=("Token Logon", password),
    )
    monitor_thread.daemon = True
    monitor_thread.start()

    try:
        subprocess.run(cmd, capture_output=True, text=True, check=True)
        return True, f"Success: Signed {len(valid_files)} files."
    except subprocess.CalledProcessError as e:
        output = "\n".join(x.strip() for x in (e.stdout, e.stderr) if x and x.strip())
        if not output:
            output = str(e)
        return False, f"Failed during signing. Error: {output}"


def sign_with_retries(signtool, file_list, password, timestamp_urls, retries_per_timestamp):
    last_message = ""
    total_attempts = len(timestamp_urls) * retries_per_timestamp
    attempt_index = 0

    for timestamp_url in timestamp_urls:
        for attempt in range(1, retries_per_timestamp + 1):
            attempt_index += 1
            print(
                "Signing attempt {}/{} with timestamp server {} ({}/{})".format(
                    attempt_index,
                    total_attempts,
                    timestamp_url,
                    attempt,
                    retries_per_timestamp,
                )
            )
            success, message = __sign_single_file(signtool, file_list, password, timestamp_url)
            print(message)
            if success:
                return True, message

            last_message = message
            if attempt < retries_per_timestamp:
                time.sleep(SIGN_RETRY_DELAY_SECONDS)

    return False, last_message


def verify_signed_files(file_list):
    unsigned = unsigned_files(file_list)
    if unsigned:
        raise RuntimeError("Signature verification failed: {}".format(", ".join(unsigned)))


def sign_single_file(file, timestamp_url=None, retries_per_timestamp=DEFAULT_SIGN_RETRIES):
    print(f"Starting signature for file {file}\n" + "-" * 50)
    return batch_sign_files(
        [file],
        timestamp_url=timestamp_url,
        retries_per_timestamp=retries_per_timestamp,
    )


def batch_sign_files(file_list, timestamp_url=None, retries_per_timestamp=DEFAULT_SIGN_RETRIES):
    """
    Main entry point to sign multiple files.
    """
    signtool = require_signtool()
    password = require_sign_password()
    timestamp_urls = resolve_timestamp_urls(timestamp_url)
    if not timestamp_urls:
        raise RuntimeError("No timestamp servers configured.")

    files = require_existing_files(file_list)
    if not files:
        print("No files need signing.")
        return True

    pending_files = unsigned_files(files)
    if not pending_files:
        print("All files are already signed.")
        return True

    print(f"Starting batch signature for {len(pending_files)} files...\n" + "-" * 50)

    success, message = sign_with_retries(
        signtool,
        pending_files,
        password,
        timestamp_urls,
        retries_per_timestamp,
    )

    if not success and len(pending_files) > 1:
        print("Batch signing failed; retrying unsigned files one by one.")
        for file in pending_files:
            if is_signed(file):
                continue
            success, message = sign_with_retries(
                signtool,
                [file],
                password,
                timestamp_urls,
                retries_per_timestamp,
            )
            if not success:
                raise RuntimeError(message)
    elif not success:
        raise RuntimeError(message)

    verify_signed_files(files)
    print("-" * 50)
    print(f"Signature verification passed for {len(files)} files.")
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Sign the supplied Windows binaries.")
    parser.add_argument("files", nargs="+", help="binaries to sign")
    batch_sign_files(parser.parse_args().files)
