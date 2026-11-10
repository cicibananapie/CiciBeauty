# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

import subprocess
import os
import sys

def staple_app(app_path):
    """Step 3: Staple the ticket to the .app so it works offline."""
    print(f"📌 Stapling ticket to {app_path}...")
    cmd = ["xcrun", "stapler", "staple", app_path]
    try:
        subprocess.run(cmd, check=True)
        print("✅ Stapling complete.")
        return True
    except subprocess.CalledProcessError as e:
        print(f"❌ Stapling failed: {e}")
        return False

if __name__ == "__main__":
    # Ensure 1 arguments are provided
    if len(sys.argv) != 2:
        print("Usage: python3 staple.py <app_path>")
        sys.exit(1)

    # Arguments from command line
    app_path = sys.argv[1]

    staple_app(app_path)
