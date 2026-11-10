# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

import subprocess
import sys
from pathlib import Path

def create_zip(app_path, zip_output):
    """Step 1: Create a ZIP using ditto to preserve symlinks and metadata."""
    print(f"📦 Creating ZIP: {zip_output}...")
    # --sequesterRsrc is critical for Apple's notarization engine
    cmd = ["/usr/bin/ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", app_path, zip_output]
    try:
        subprocess.run(cmd, check=True)
        print("✅ ZIP created successfully.")
        return True
    except subprocess.CalledProcessError as e:
        print(f"❌ Failed to create ZIP: {e}")
        return False


def create_update_zip(app_path, zip_output):
    """Create an updater ZIP whose archive root is the app's Contents directory."""
    app = Path(app_path).resolve()
    output = Path(zip_output).resolve()
    print(f"📦 Creating updater ZIP: {output}...")
    # ZipWrapper restores executable bits, but does not restore AppleDouble metadata.
    cmd = ["/usr/bin/ditto", "-c", "-k", "--norsrc", "--noextattr", "--noqtn", "--noacl", ".", str(output)]
    try:
        subprocess.run(cmd, check=True, cwd=app)
        print("✅ Updater ZIP created successfully.")
        return True
    except subprocess.CalledProcessError as e:
        print(f"❌ Failed to create updater ZIP: {e}")
        return False

if __name__ == "__main__":
    # Ensure 2 arguments are provided
    if len(sys.argv) != 3:
        print("Usage: python3 dittozip.py <app_path> <zip_output>")
        sys.exit(1)

    # Arguments from command line
    app_path = sys.argv[1]
    zip_output = sys.argv[2]

    create_zip(app_path, zip_output)
