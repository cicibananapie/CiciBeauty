// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <array>
#include <utility>

#include "message.h"

namespace cici::protocol {

inline constexpr std::array<const char*, 8> kEffectCatalogNames = {
    "filters", "stylizedMakeup", "customizedMakeup", "aiEffects",
    "specialEffects", "cameraEffects", "cameraMovement", "shotAtmosphere",
};

inline JsonMessage requestCatalogSnapshot() {
    return {"catalog.getSnapshot", nlohmann::json::object()};
}

// body: catalogs keyed by kEffectCatalogNames, and effects containing id, hv,
// and package (null for bundled effects, otherwise the assetJson representation).
// Runtime package paths remain in luma.
inline JsonMessage catalogSnapshot(nlohmann::json body) {
    return {"catalog.snapshot", std::move(body)};
}

} // namespace cici::protocol
