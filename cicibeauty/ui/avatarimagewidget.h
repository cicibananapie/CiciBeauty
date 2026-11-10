// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QMetaObject>
#include <QPixmap>
#include <QPointer>
#include <QWidget>

class GlobalModel;
class RemoteImageStatus;

class AvatarImageWidget : public QWidget {
public:
    explicit AvatarImageWidget(GlobalModel* app, QWidget* parent = nullptr);

    void setAvatarUrl(const QString& url);
    void setFallbackText(const QString& text);
    void clear();
    void setDiameter(int diameter);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void clearRemoteImageBinding();
    void loadLocalImage(const QString& path);
    void setDefaultAvatar();

private:
    GlobalModel* app_ = nullptr;
    QString avatarUrl_;
    QString fallbackText_;
    QPixmap pixmap_;
    int diameter_ = 32;
    QPointer<RemoteImageStatus> remoteImageStatus_;
    QMetaObject::Connection remoteImageStateConnection_;
};
