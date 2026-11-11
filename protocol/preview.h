// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <cstdint>

namespace cici::protocol {

enum class PreviewSource : uint32_t {
    Input = 1,
    InputWithMagnifier,
    Output
};

inline constexpr uint32_t kPreviewSlots = 3;

// One FIFO record leases the corresponding GPU texture until releaseRead().
// No pixels or process-local pointers are stored in this queue.
struct PreviewFrame {
    uint32_t slot = 0;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
};

} // namespace cici::protocol
