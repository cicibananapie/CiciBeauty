// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "avatarimagewidget.h"

#include <algorithm>

#include <QPainter>
#include <QPainterPath>
#include <QTextBoundaryFinder>

#include "assetsmgr.h"
#include "../modelapp.h"

namespace {

QString firstDisplayChar(const QString& text) {
	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty()) {
		return QStringLiteral("C");
	}

	QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, trimmed);
	const auto end = finder.toNextBoundary();
	if (end > 0) {
		return trimmed.left(end).toUpper();
	}

	return trimmed.left(1).toUpper();
}
} // namespace

AvatarImageWidget::AvatarImageWidget(GlobalModel* app, QWidget* parent)
    : QWidget(parent)
    , app_(app) {
    setDiameter(diameter_);
    setDefaultAvatar();
}

void AvatarImageWidget::setDiameter(int diameter) {
    diameter_ = std::max(1, diameter);
    setFixedSize(diameter_, diameter_);
    update();
}

void AvatarImageWidget::setAvatarUrl(const QString& url) {
	clearRemoteImageBinding();
	avatarUrl_ = url.trimmed();
	setDefaultAvatar();
	if (avatarUrl_.isEmpty()) {
		return;
    }

    auto* status = app_->assets()->requestRemoteImage(avatarUrl_);
    remoteImageStatus_ = status;
    remoteImageStateConnection_ = QObject::connect(
        status,
        &RemoteImageStatus::stateChanged,
        this,
        [this, status](RemoteImageState state) {
            if (remoteImageStatus_ != status || state != RemoteImageState::Ready) {
                return;
            }
            loadLocalImage(status->localPath());
        });

    if (status->state() == RemoteImageState::Ready) {
        loadLocalImage(status->localPath());
    }
}

void AvatarImageWidget::setFallbackText(const QString& text) {
    fallbackText_ = text;
    update();
}

void AvatarImageWidget::clear() {
    clearRemoteImageBinding();
    avatarUrl_.clear();
    fallbackText_.clear();
    setDefaultAvatar();
}

void AvatarImageWidget::paintEvent(QPaintEvent* event) {
    (void)event;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF avatarRect = rect().adjusted(1, 1, -1, -1);
    QPainterPath clipPath;
    clipPath.addEllipse(avatarRect);

    painter.fillPath(clipPath, QColor(64, 74, 90));

    if (!pixmap_.isNull()) {
        painter.save();
        painter.setClipPath(clipPath);

        const int target = static_cast<int>(avatarRect.width());
        QPixmap scaled = pixmap_.scaled(target,
                                        target,
                                        Qt::KeepAspectRatioByExpanding,
                                        Qt::SmoothTransformation);
        const QRect src((scaled.width() - target) / 2,
                        (scaled.height() - target) / 2,
                        target,
                        target);
        painter.drawPixmap(avatarRect.toRect(), scaled, src);
        painter.restore();
        return;
    }

    QFont f = painter.font();
    f.setBold(true);
    f.setPixelSize(std::max(12, diameter_ / 2));
    painter.setFont(f);
    painter.setPen(QColor(232, 237, 245));
    painter.drawText(avatarRect, Qt::AlignCenter, firstDisplayChar(fallbackText_));
}

void AvatarImageWidget::loadLocalImage(const QString& path) {
    QPixmap pixmap;
    if (!path.isEmpty() && pixmap.load(path)) {
        pixmap_ = pixmap;
    } else {
        setDefaultAvatar();
    }
    update();
}

void AvatarImageWidget::clearRemoteImageBinding() {
    QObject::disconnect(remoteImageStateConnection_);
    remoteImageStateConnection_ = {};
    remoteImageStatus_ = nullptr;
}

void AvatarImageWidget::setDefaultAvatar() {
    pixmap_ = QPixmap();
    update();
}
