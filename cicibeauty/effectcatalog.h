// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "protocol/assets.h"

class EffectCatalog {
public:
    struct EffectInfo {
        uint64_t id_ = 0;
        std::optional<EffectAsset> package_;
        uint32_t hv_ = 0;
    };

    EffectCatalog();
    ~EffectCatalog();
    EffectCatalog(const EffectCatalog&) = delete;
    EffectCatalog& operator=(const EffectCatalog&) = delete;

    void load();
    bool isReady() const;
    std::function<void()> ready;

    const std::vector<EffectInfo>& allEffects() const;
    const EffectInfo* effectInfo(uint64_t id) const;

    const nlohmann::json& filtersJson() const;
    const nlohmann::json& stylizedMakeupJson() const;
    const nlohmann::json& customizedMakeupJson() const;
    const nlohmann::json& aiEffectsJson() const;
    const nlohmann::json& specialEffectsJson() const;
    const nlohmann::json& cameraEffectsJson() const;
    const nlohmann::json& cameraMovementJson() const;
    const nlohmann::json& shotAtmosphereJson() const;

private:
    bool applySnapshot(const nlohmann::json& body);

    nlohmann::json catalogs_ = nlohmann::json::object();
    std::vector<EffectInfo> effects_;
    std::unordered_map<uint64_t, size_t> effectIndex_;
    bool ready_ = false;
};
