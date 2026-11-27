// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

import AppKit

// Finder's file icon is metadata; the mounted volume icon is embedded by create-dmg.
let icon = NSImage(contentsOfFile: CommandLine.arguments[1])
guard let icon,
      NSWorkspace.shared.setIcon(icon, forFile: CommandLine.arguments[2], options: []) else {
    fputs("Failed to set the DMG file icon.\n", stderr)
    exit(1)
}
