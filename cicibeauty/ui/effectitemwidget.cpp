// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "effectitemwidget.h"

#include <assert.h>

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QString>
#include <QTimerEvent>

#include "assetsmgr.h"
#include "../modelapp.h"

namespace {

constexpr int kDownloadingTimerIntervalMs = 50;
constexpr int kDownloadingAngleStep = 24;
constexpr int kRetryHitTargetSize = 44;
constexpr int kEffectIconSize = 56;

} // namespace

EffectItemWidget::EffectItemWidget(GlobalModel* app, QWidget* parent)
    : ImageTextWidget(app->assets(), parent)
    , app_(app) {
    assert(app_ != nullptr);
    setImageDisplaySize(QSize(kEffectIconSize, kEffectIconSize));
}

EffectItemWidget::~EffectItemWidget() {
    stopDownloadingAnimation();
}

void EffectItemWidget::setId(uint32_t id) {
    ImageTextWidget::setId(id);
    const QString automationName = QStringLiteral("effectItem_%1").arg(id);
    setObjectName(automationName);
    setAccessibleName(automationName);
}

void EffectItemWidget::bindEffect(const EffectCatalog::EffectInfo& effect) {
    assert(app_ != nullptr);
    if (app_ == nullptr) {
        return;
    }

    if (!effect.package_.has_value()) {
        QObject::disconnect(packageStateConnection_);
        packageStatus_ = nullptr;
        bindingKind_ = BindingKind::Bundled;
        stopDownloadingAnimation();
        update();
        return;
    }

    EffectPackageStatus* status = app_->assets()->effectPackageStatus(*effect.package_);
    bindingKind_ = BindingKind::Remote;
    if (packageStatus_ == status) {
        applyPackageState(status->state());
        return;
    }

    QObject::disconnect(packageStateConnection_);
    packageStatus_ = status;
    packageStateConnection_ = QObject::connect(
        packageStatus_,
        &EffectPackageStatus::stateChanged,
        this,
        [this](EffectPackageState state) {
            applyPackageState(state);
        });
    applyPackageState(packageStatus_->state());
}

EffectPackageStatus* EffectItemWidget::packageStatus() const {
    return packageStatus_;
}

bool EffectItemWidget::packageReady() const {
    switch (bindingKind_) {
    case BindingKind::Bundled:
        return true;
    case BindingKind::Remote:
        return packageStatus_ != nullptr
            && packageStatus_->state() == EffectPackageState::Ready;
    case BindingKind::Unbound:
        return false;
    }
    return false;
}

void EffectItemWidget::mousePressEvent(QMouseEvent* event) {
    if (isEnabled() && event->button() == Qt::LeftButton && packageStatus_ != nullptr &&
        (packageStatus_->state() == EffectPackageState::Failed ||
         packageStatus_->state() == EffectPackageState::Unknown) &&
        packageStateRect(kRetryHitTargetSize).contains(event->pos())) {
        if (auto* runtime = app_->filterModel(); runtime != nullptr) {
            runtime->retryEffectPackageDownload(id());
        }
        event->accept();
        return;
    }

    ImageTextWidget::mousePressEvent(event);
}

void EffectItemWidget::paintEvent(QPaintEvent* event) {
    ImageTextWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    drawPackageState(&painter);
}

void EffectItemWidget::timerEvent(QTimerEvent* event) {
    if (downloadingTimer_.isActive()
        && event->timerId() == downloadingTimer_.timerId()) {
        downloadingAngle_ = (downloadingAngle_ + kDownloadingAngleStep) % 360;
        update();
        return;
    }

    ImageTextWidget::timerEvent(event);
}

void EffectItemWidget::applyPackageState(EffectPackageState state) {
    if (state == EffectPackageState::Downloading) {
        startDownloadingAnimation();
    } else {
        stopDownloadingAnimation();
    }
    update();
}

void EffectItemWidget::drawPackageState(QPainter* painter) const {
    if (packageStatus_ == nullptr) {
        return;
    }

    const EffectPackageState state = packageStatus_->state();
    if (state == EffectPackageState::Ready) {
        return;
    }

    const bool hovered = underMouse();
    const bool retryable = state == EffectPackageState::Failed || state == EffectPackageState::Unknown;
    if (state == EffectPackageState::Missing && !hovered) {
        return;
    }
    if (retryable && !isSelected() && !hovered) {
        return;
    }

    if (state == EffectPackageState::Missing) {
        const QRect badge = packageStateRect(24);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 145));
        painter->drawEllipse(badge);

        const QPoint center = badge.center();
        painter->setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawLine(center.x(), center.y() - 6, center.x(), center.y() + 3);
        painter->drawLine(center.x() - 4, center.y(), center.x(), center.y() + 4);
        painter->drawLine(center.x(), center.y() + 4, center.x() + 4, center.y());
        painter->drawLine(center.x() - 5, center.y() + 7, center.x() + 5, center.y() + 7);
        return;
    }

    const QRect badge = packageStateRect(34);
    painter->setPen(Qt::NoPen);
    painter->setBrush(retryable ? QColor(120, 24, 24, 205) : QColor(0, 0, 0, 180));
    painter->drawEllipse(badge);

    const QRect arc = badge.adjusted(8, 8, -8, -8);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (state == EffectPackageState::Downloading) {
        painter->drawArc(arc, downloadingAngle_ * 16, 250 * 16);
        return;
    }

    painter->drawArc(arc, 35 * 16, 285 * 16);
    const QPoint arrow = arc.topRight() + QPoint(-1, 3);
    painter->drawLine(arrow, arrow + QPoint(-5, 0));
    painter->drawLine(arrow, arrow + QPoint(0, 5));
}

QRect EffectItemWidget::packageStateRect(int size) const {
    const QPoint center(width() / 2, height() / 2 - 6);
    return QRect(center.x() - size / 2,
                 center.y() - size / 2,
                 size,
                 size);
}

void EffectItemWidget::startDownloadingAnimation() {
    if (!downloadingTimer_.isActive()) {
        downloadingTimer_.start(kDownloadingTimerIntervalMs, this);
    }
}

void EffectItemWidget::stopDownloadingAnimation() {
    if (downloadingTimer_.isActive()) {
        downloadingTimer_.stop();
    }
    downloadingAngle_ = 0;
}
