// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <QPixmap>

#include "cicibase.h"
#include "log.h"
#include "assetsmgr.h"
#include "../lumaclient.h"
#include "../qstringconv.h"

AssetsMgr::AssetsMgr() {
    LumaClient::instance().addListener(
        this,
        [this](const cici::protocol::JsonMessage& message) {
            onMessage(message);
        },
        [this]() {
            onClosed();
        });
}

AssetsMgr::~AssetsMgr() {
    LumaClient::instance().removeListener(this);
}

EffectPackageStatus::EffectPackageStatus(EffectAsset package) : package_(std::move(package)) {
}

EffectPackageState EffectPackageStatus::state() const {
    return state_;
}

const EffectAsset& EffectPackageStatus::package() const {
    return package_;
}

const std::u8string& EffectPackageStatus::cacheKey() const {
    return package_.sha256_;
}

void EffectPackageStatus::setState(EffectPackageState state) {
    if (state_ == state) {
        return;
    }

    state_ = state;
    emit stateChanged(state_);
}

RemoteImageStatus::RemoteImageStatus(QString url, QString localPath)
    : url_(std::move(url))
    , localPath_(std::move(localPath)) {
}

RemoteImageState RemoteImageStatus::state() const {
    return state_;
}

const QString& RemoteImageStatus::url() const {
    return url_;
}

const QString& RemoteImageStatus::localPath() const {
    return localPath_;
}

void RemoteImageStatus::setState(RemoteImageState state) {
    if (state_ == state) {
        return;
    }

    state_ = state;
    emit stateChanged(state_);
}

QPixmap AssetsMgr::image(const QString& path) {
    auto it = imageCache_.find(path);
    if (it == imageCache_.end()) {
        QPixmap pix;
        if (!pix.load(path)) {
            LogError << "Failed to load image: " << to_u8(path) << logEnd;
            assert(false);
        }

        it = imageCache_.insert(path, pix);
    }

    return it.value();
}

RemoteImageStatus* AssetsMgr::requestRemoteImage(const QString& url) {
    assert(isMainThread());
    const auto key = to_u8(url);
    auto it = remoteImages_.find(key);
    if (it == remoteImages_.end()) {
        auto status = std::shared_ptr<RemoteImageStatus>(new RemoteImageStatus(url, {}));
        it = remoteImages_.emplace(key, std::move(status)).first;
        startRemoteImageRequest(it->second);
    } else if (it->second->state() == RemoteImageState::Failed) {
        it->second->setState(RemoteImageState::Loading);
        startRemoteImageRequest(it->second);
    }
    return it->second.get();
}

void AssetsMgr::startRemoteImageRequest(const std::shared_ptr<RemoteImageStatus>& status) {
    LumaClient::instance().callAsync(
        status.get(), "assets.requestRemoteImage", {{"url", to_json(to_u8(status->url()))}},
        [status = status.get()](cici::protocol::CallResult result) {
            if (result.ok() && result.body.contains("localPath") && result.body["localPath"].is_string() &&
                !result.body["localPath"].get_ref<const std::string&>().empty()) {
                status->localPath_ = from_u8(to_u8(result.body["localPath"].get<std::string>()));
                status->setState(RemoteImageState::Ready);
            } else {
                status->localPath_.clear();
                status->setState(RemoteImageState::Failed);
            }
        });
}

EffectPackageStatus* AssetsMgr::effectPackageStatus(const EffectAsset& package) {
    assert(isMainThread());
    auto it = effectPackages_.find(package.sha256_);
    if (it == effectPackages_.end()) {
        auto status = std::shared_ptr<EffectPackageStatus>(new EffectPackageStatus(package));
        it = effectPackages_.emplace(package.sha256_, std::move(status)).first;
    }
    auto* status = it->second.get();
    if (!package.valid()) {
        status->setState(EffectPackageState::Failed);
        return status;
    }
    if (status->state() == EffectPackageState::Unknown && !status->querying_) {
        status->querying_ = true;
        LumaClient::instance().callAsync(
            status, "assets.queryEffectPackageStatus", {{"package", cici::protocol::assetJson(package)}},
            [status](cici::protocol::CallResult result) {
                status->querying_ = false;
                EffectPackageState state;
                if (result.ok() && result.body.contains("state") &&
                    cici::protocol::readAssetState(result.body["state"], state, EffectPackageState::Failed)) {
                    status->setState(state);
                } else {
                    LogWarn << "AssetsMgr package status query failed, hash:" << status->cacheKey()
                            << " IPC status:" << static_cast<int>(result.status) << logEnd;
                }
            });
    }
    return status;
}

void AssetsMgr::downloadEffectPackage(const EffectAsset& package,
                                      const EffectOperationContext& context) {
    assert(isMainThread());
    auto* status = effectPackageStatus(package);
    if (!package.valid() || status->state() == EffectPackageState::Unknown ||
        status->state() == EffectPackageState::Ready || status->state() == EffectPackageState::Downloading) {
        return;
    }
    const bool sent = LumaClient::instance().postMsg(cici::protocol::downloadEffectPackage(package, context));
    status->setState(sent ? EffectPackageState::Downloading : EffectPackageState::Failed);
}

void AssetsMgr::onMessage(const cici::protocol::JsonMessage& message) {
    const auto& method = message.value["method"];
    const auto& body = message.value["body"];
    if (method == "assets.effectPackageStateChanged") {
        EffectPackageState state;
        if (!body.contains("sha256") || !body["sha256"].is_string() || !body.contains("state") ||
            !cici::protocol::readAssetState(body["state"], state, EffectPackageState::Failed)) {
            LogWarn << "invalid effect package state notification" << logEnd;
            return;
        }
        const auto it = effectPackages_.find(to_u8(body["sha256"].get<std::string>()));
        if (it != effectPackages_.end()) {
            it->second->setState(state);
        }
    }
}

void AssetsMgr::onClosed() {
    for (const auto& [url, status] : remoteImages_) {
        if (status->state() == RemoteImageState::Loading) {
            status->setState(RemoteImageState::Failed);
        }
    }
    for (const auto& [hash, status] : effectPackages_) {
        status->setState(EffectPackageState::Unknown);
    }
}
