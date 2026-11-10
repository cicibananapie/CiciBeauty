# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

"""Create the existing direct-run installer disk image layout."""

import shutil
import subprocess
import tempfile
from pathlib import Path


def create_direct_run_dmg(app: Path, output: Path) -> None:
    tool = shutil.which("create-dmg")
    if tool is None:
        raise RuntimeError("create-dmg was not found. Install it with: brew install create-dmg")
    if output.exists():
        raise FileExistsError(output)
    name = "Cici Beauty Setup.app"
    with tempfile.TemporaryDirectory(prefix="cici-dmg-") as directory:
        shutil.copytree(app, Path(directory) / name, symlinks=True)
        subprocess.run([
            tool, "--volname", "Cici Beauty Setup", "--window-pos", "200", "120",
            "--window-size", "520", "340", "--icon-size", "128",
            "--icon", name, "260", "160", "--hide-extension", name,
            "--no-internet-enable", str(output), directory,
        ], check=True)
