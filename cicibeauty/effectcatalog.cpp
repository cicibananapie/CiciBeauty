// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "effectcatalog.h"

#include <limits>
#include <utility>

#include "log.h"
#include "lumaclient.h"
#include "protocol/catalog.h"

EffectCatalog::EffectCatalog() {
    for (const auto* name : cici::protocol::kEffectCatalogNames) {
        catalogs_[name] = nlohmann::json::object();
    }
}

EffectCatalog::~EffectCatalog() {
    LumaClient::instance().removeListener(this);
}

void EffectCatalog::load() {
    LumaClient::instance().addListener(
        this,
        [this](const cici::protocol::JsonMessage& message) {
            if (message.value["method"] != "catalog.snapshot") {
                return;
            }
            if (!applySnapshot(message.value["body"])) {
                LogError << "invalid effect catalog snapshot" << logEnd;
                return;
            }
            ready_ = true;
            if (ready) {
                ready();
            }
        },
        [this]() {
            ready_ = false;
            LogWarn << "effect catalog service disconnected" << logEnd;
        },
        []() {
            if (!LumaClient::instance().postMsg(cici::protocol::requestCatalogSnapshot())) {
                LogError << "request effect catalog snapshot failed" << logEnd;
            }
        });
}

bool EffectCatalog::applySnapshot(const nlohmann::json& body) {
    const auto catalogs = body.find("catalogs");
    const auto items = body.find("effects");
    if (catalogs == body.end() || !catalogs->is_object() || items == body.end() || !items->is_array()) {
        return false;
    }
    for (const auto* name : cici::protocol::kEffectCatalogNames) {
        const auto value = catalogs->find(name);
        if (value == catalogs->end() || !value->is_object()) {
            return false;
        }
    }

    std::vector<EffectInfo> effects;
    std::unordered_map<uint64_t, size_t> index;
    for (const auto& item : *items) {
        if (!item.is_object() || !item.contains("id") || !item["id"].is_number_unsigned() ||
            item["id"].get<uint64_t>() == 0 || !item.contains("hv") || !item["hv"].is_number_unsigned() ||
            item["hv"].get<uint64_t>() > std::numeric_limits<uint32_t>::max() || !item.contains("package")) {
            return false;
        }
        EffectInfo effect;
        effect.id_ = item["id"].get<uint64_t>();
        effect.hv_ = item["hv"].get<uint32_t>();
        if (!item["package"].is_null()) {
            EffectAsset asset;
            if (!cici::protocol::readAsset(item["package"], asset)) {
                return false;
            }
            effect.package_ = std::move(asset);
        }
        if (!index.emplace(effect.id_, effects.size()).second) {
            return false;
        }
        effects.push_back(std::move(effect));
    }

    catalogs_ = *catalogs;
    effects_ = std::move(effects);
    effectIndex_ = std::move(index);
    return true;
}

bool EffectCatalog::isReady() const {
    return ready_;
}

const std::vector<EffectCatalog::EffectInfo>& EffectCatalog::allEffects() const {
    return effects_;
}

const EffectCatalog::EffectInfo* EffectCatalog::effectInfo(uint64_t id) const {
    const auto it = effectIndex_.find(id);
    if (it == effectIndex_.end()) {
        return nullptr;
    }
    return &effects_[it->second];
}

const nlohmann::json& EffectCatalog::filtersJson() const {
    return catalogs_["filters"];
}

const nlohmann::json& EffectCatalog::stylizedMakeupJson() const {
    return catalogs_["stylizedMakeup"];
}

const nlohmann::json& EffectCatalog::customizedMakeupJson() const {
    return catalogs_["customizedMakeup"];
}

const nlohmann::json& EffectCatalog::aiEffectsJson() const {
    return catalogs_["aiEffects"];
}

const nlohmann::json& EffectCatalog::specialEffectsJson() const {
    return catalogs_["specialEffects"];
}

const nlohmann::json& EffectCatalog::cameraEffectsJson() const {
    return catalogs_["cameraEffects"];
}

const nlohmann::json& EffectCatalog::cameraMovementJson() const {
    return catalogs_["cameraMovement"];
}

const nlohmann::json& EffectCatalog::shotAtmosphereJson() const {
    return catalogs_["shotAtmosphere"];
}
