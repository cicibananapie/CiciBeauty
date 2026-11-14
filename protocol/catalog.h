// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <array>

namespace cici::protocol {

inline constexpr std::array<const char*, 8> kEffectCatalogNames = {
    "filters", "stylizedMakeup", "customizedMakeup", "aiEffects",
    "specialEffects", "cameraEffects", "cameraMovement", "shotAtmosphere",
};

// catalog.getSnapshot: empty request body, asynchronous reply containing
// catalogs keyed by kEffectCatalogNames, and effects containing id, hv,
// and package (null for bundled effects, otherwise the assetJson representation).
// Runtime package paths remain in luma.

} // namespace cici::protocol
