# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

import subprocess
import os
import sys
import re
import json

DEFAULT_NOTARY_PROFILE = "notarytool-password"

def extract_submission_id(output_text):
    # Matches a UUID pattern following "id: "
    pattern = r"id:\s+([a-fA-F0-9-]{36})"
    match = re.search(pattern, output_text)
    
    if match:
        return match.group(1)
    return None

def fetch_notary_log(submission_id: str, profile_name: str):
    """
    Fetches and prints the detailed notarization log for a specific ID.
    """
    print(f"🔍 Fetching logs for Submission ID: {submission_id}...")

    # Build the command. 
    # We don't provide a filename so it outputs directly to stdout.
    cmd = [
        "xcrun", "notarytool", "log", 
        submission_id, 
        "--keychain-profile", profile_name
    ]

    try:
        # Run command and capture output
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        
        # Parse the JSON output
        log_data = json.loads(result.stdout)
        
        # Pretty print the entire log
        print("\n=== Apple Notary Log Detail ===")
        print(json.dumps(log_data, indent=2, ensure_ascii=False))
        
        # Specifically highlight issues if they exist
        issues = log_data.get("issues", [])
        if issues:
            print(f"\n❌ Found {len(issues)} issues:")
            for issue in issues:
                print(f"- Path: {issue.get('path')}")
                print(f"Message: {issue.get('message')}")
        else:
            print("\n✅ No issues found in the log.")

    except subprocess.CalledProcessError as e:
        print(f"❌ Failed to fetch log: {e.stderr}")
    except json.JSONDecodeError:
        print("❌ Could not parse log output as JSON.")
        print(f"Raw Output: {result.stdout}")

# Example Usage
# fetch_notary_log("3c5e02f7-c086-4bc1-be0d-0f521cadf8d9", "YourProfileName")


def resolve_notary_profile(profile_name: str = ""):
    if profile_name and profile_name.strip():
        return profile_name.strip()

    profile_from_env = os.getenv("CICI_NOTARY_PROFILE", "").strip()
    if profile_from_env:
        return profile_from_env

    return DEFAULT_NOTARY_PROFILE


def notarize_file(file_to_notarize, profile_name=""):
    """Step 2: Submit the file (ZIP or DMG) to Apple for scanning."""
    profile = resolve_notary_profile(profile_name)
    print(f"📤 Submitting {file_to_notarize} for Notarization...")
    print(f"🔑 Using keychain profile: {profile}")
    # --wait blocks until the status is 'Accepted' or 'Invalid'
    cmd = ["xcrun", "notarytool", "submit", file_to_notarize, 
           "--keychain-profile", profile, "--wait"]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        print(result.stdout)
        if "status: Accepted" in result.stdout:
            print("✅ Notarization Accepted!")
            return True
        else:
            submission_id = extract_submission_id(result.stdout)
            if submission_id:
                fetch_notary_log(submission_id, profile)
            else:
                print("⚠️ Could not parse Submission ID from notarytool output.")
    except subprocess.CalledProcessError as e:
        print(f"❌ Notarization failed: {e.stderr}")
        if "401" in e.stderr or "Invalid credentials" in e.stderr:
            print("💡 Hint: refresh keychain profile with a valid app-specific password.")
            print("💡 Hint: ensure Apple ID / Team ID matches the Developer ID signing certificate.")
    return False

if __name__ == "__main__":
    # Ensure 1 arguments are provided
    if len(sys.argv) != 2:
        print("Usage: python3 notarize.py <file_to_notarize>")
        sys.exit(1)

    # Arguments from command line
    file_to_notarize = sys.argv[1]

    notarize_file(file_to_notarize)
