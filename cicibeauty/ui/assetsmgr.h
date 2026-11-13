// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <QString>
#include <QMap>
#include <QMetaType>
#include <QObject>

#include "protocol/assets.h"

class QPixmap;
struct EffectOperationContext;

Q_DECLARE_METATYPE(EffectPackageState)

class EffectPackageStatus final : public QObject {
    Q_OBJECT

public:
    ~EffectPackageStatus() override = default;

    EffectPackageState state() const;
    const EffectAsset& package() const;
    const std::u8string& cacheKey() const;

signals:
    void stateChanged(EffectPackageState state);

private:
    friend class AssetsMgr;

    explicit EffectPackageStatus(EffectAsset package);
    void setState(EffectPackageState state);

private:
    EffectAsset package_;
    EffectPackageState state_ = EffectPackageState::Unknown;
    bool querying_ = false;
};

Q_DECLARE_METATYPE(RemoteImageState)

class RemoteImageStatus final : public QObject {
    Q_OBJECT

public:
    ~RemoteImageStatus() override = default;

    RemoteImageState state() const;
    const QString& url() const;
    const QString& localPath() const;

signals:
    void stateChanged(RemoteImageState state);

private:
    friend class AssetsMgr;

    RemoteImageStatus(QString url, QString localPath);
    void setState(RemoteImageState state);

private:
    QString url_;
    QString localPath_;
    RemoteImageState state_ = RemoteImageState::Loading;
};

class AssetsMgr {
public:
    AssetsMgr();
    ~AssetsMgr();

    AssetsMgr(const AssetsMgr&) = delete;
    AssetsMgr& operator=(const AssetsMgr&) = delete;
    
public:
    QPixmap image(const QString& path);
    RemoteImageStatus* requestRemoteImage(const QString& url);

    // Remote image and package status APIs are accessed on the UI thread.
    EffectPackageStatus* effectPackageStatus(const EffectAsset& package);
    void downloadEffectPackage(const EffectAsset& package,
                               const EffectOperationContext& context);

private:
    void startRemoteImageRequest(const std::shared_ptr<RemoteImageStatus>& status);
    void onMessage(const cici::protocol::JsonMessage& message);
    void onClosed();

private:
    QMap<QString, QPixmap> imageCache_;
    std::map<std::u8string, std::shared_ptr<RemoteImageStatus>> remoteImages_;
    std::map<std::u8string, std::shared_ptr<EffectPackageStatus>> effectPackages_;
};
