// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>
#include <algorithm>
#include <cmath>

#include <QColor>
#include <QBrush>
#include <QPen>
#include <QPainter>
#include <QMouseEvent>
#include <QUuid>

#include "nokeyingzonewidget.h"

namespace {
constexpr int kMinZoneSize = 20;
constexpr int kDeleteButtonSize = 10;

double clampNormalized(double v) {
    return std::clamp(v, 0.0, 1.0);
}

double normalizePosition(int value, int maxValue) {
    if (maxValue <= 0) {
        return 0.0;
    }

    return clampNormalized(static_cast<double>(value) / static_cast<double>(maxValue));
}

QString createZoneTag() {
    return QString("nokeying_mask_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}
}

NokeyingZoneWidget::ZoneData::ZoneData() {
    x = 0;
    y = 0;
    width = 0;
    height = 0;
    type = Rectangle;
}

NokeyingZoneWidget::ZoneData::ZoneData(const ZoneData& r) {
    tag = r.tag;
    x = r.x;
    y = r.y;
    width = r.width;
    height = r.height;
    type = r.type;
}

bool NokeyingZoneWidget::ZoneData::contain(const QPoint& pt, const QSize& canvasSize) const {
    return toPixelRect(canvasSize).contains(pt);
}

void NokeyingZoneWidget::ZoneData::reset() {
    tag.clear();
    x = 0;
    y = 0;
    width = 0;
    height = 0;
    type = Rectangle;
}

QRect NokeyingZoneWidget::ZoneData::toPixelRect(const QSize& canvasSize) const {
    const int px = static_cast<int>(std::lround(x * canvasSize.width()));
    const int py = static_cast<int>(std::lround(y * canvasSize.height()));
    const int pw = static_cast<int>(std::lround(width * canvasSize.width()));
    const int ph = static_cast<int>(std::lround(height * canvasSize.height()));

    return QRect(px, py, pw, ph);
}

bool NokeyingZoneWidget::ZoneData::isValid(const QSize& canvasSize) const {
    const QRect rc = toPixelRect(canvasSize);
    return rc.width() > kMinZoneSize && rc.height() > kMinZoneSize;
}

NokeyingZoneWidget::NokeyingZoneWidget(GlobalModel *app, QWidget *parent /* = nullptr*/)
    : GPURenderWidget(app, parent) {
    
}

NokeyingZoneWidget::~NokeyingZoneWidget() {
    for (auto i = 0; i < zones_.size(); i++) {
        delete zones_.at(i);
    }
    zones_.clear();
}

GPURenderWidget::NativeRenderSourceType NokeyingZoneWidget::nativeRenderSourceType() const {
    return NativeRenderSourceType::Input;
}

void NokeyingZoneWidget::clearZoneData() {
    for (auto i = 0; i < zones_.size(); i++) {
        delete zones_.at(i);
    }
    zones_.clear();
    selectedZone_ = nullptr;
    zoneCreating_ = false;
    typeUsed_ = Rectangle;
    candidateZone_.reset();
    updateOverlay();
}

void NokeyingZoneWidget::setZones(const QList<ZoneData>& zones) {
    clearZoneData();
    for (const auto& zone : zones) {
        zones_.append(new ZoneData(zone));
    }
    updateOverlay();
}

void NokeyingZoneWidget::paintOverlay(QPainter& painter) {
    QBrush brush(QColor(245, 222, 179, 128));
    painter.setBrush(brush);
    painter.setPen(QColor(255, 255, 0, 255));
    for (auto i = 0; i < zones_.count(); i++) {
        ZoneData* z = zones_.at(i);
        const QRect rc = z->toPixelRect(size());
        if (z->type == Rectangle) {
            painter.drawRect(rc);
        }
        else {
            assert(z->type == Circle);
            painter.drawEllipse(rc);
        }
    }
    
    if (selectedZone_ != nullptr) {
        assert(!zoneCreating_);
        const QRect rc = selectedZone_->toPixelRect(size());
        painter.drawLine(rc.x(), rc.y(), rc.x() + rc.width(), rc.y());
        painter.drawLine(rc.x() + rc.width(), rc.y(), rc.x() + rc.width(), rc.y() + rc.height());
        painter.drawLine(rc.x() + rc.width(), rc.y() + rc.height(), rc.x(), rc.y() + rc.height());
        painter.drawLine(rc.x(), rc.y() + rc.height(), rc.x(), rc.y());
        
        QBrush brush(QColor(255, 0, 0, 255));
        painter.setBrush(brush);
        painter.setPen(QColor(255, 255, 255, 255));
        QRect closeRc(rc.x() + rc.width() - kDeleteButtonSize, rc.y(), kDeleteButtonSize, kDeleteButtonSize);
        painter.drawRect(closeRc);
        
        QPoint p1(closeRc.x() + 1, closeRc.y() + 1);
        QPoint p2(closeRc.x() + 9, closeRc.y() + 9 );
        QPoint p3(closeRc.x() + 9, closeRc.y() + 1);
        QPoint p4(closeRc.x() + 1, closeRc.y() + 9);
        painter.drawLine(p1, p2);
        painter.drawLine(p3, p4);
    }
    
    if (zoneCreating_ && candidateZone_.isValid(size())) {
        assert(selectedZone_ == nullptr);
        const QRect rc = candidateZone_.toPixelRect(size());
        if (candidateZone_.type == Rectangle) {
            painter.drawRect(rc);
        }
        else {
            assert(candidateZone_.type == Circle);
            painter.drawEllipse(rc);
        }
    }
}

void NokeyingZoneWidget::mousePressEvent(QMouseEvent *event) {
    ZoneData* preSelected = selectedZone_;
    for (auto it = zones_.begin(); it != zones_.end(); it++) {
        ZoneData* z = *it;
        QPoint pt(event->pos());
        if (z->contain(pt, size())) {
            selectedZone_ = z;
            const QRect zoneRc = z->toPixelRect(size());
            QRect closeRc(zoneRc.x() + zoneRc.width() - kDeleteButtonSize, zoneRc.y(), kDeleteButtonSize, kDeleteButtonSize);
            if(closeRc.contains(pt) && preSelected == selectedZone_) {
                zones_.erase(it);
                emit zoneDeleted(z->tag);
                delete z;
                selectedZone_ = nullptr;
                updateOverlay();
            }
            
            GPURenderWidget::mousePressEvent(event);
            return ;
        }
    }
    
    selectedZone_ = nullptr;
    zoneCreating_ = true;
    candidateZone_.reset();
    candidateZone_.tag = createZoneTag();
    candidateZone_.type = typeUsed_;
    candidateZone_.x = normalizePosition(event->pos().x(), width());
    candidateZone_.y = normalizePosition(event->pos().y(), height());
    updateOverlay();
    
    GPURenderWidget::mousePressEvent(event);
}

void NokeyingZoneWidget::mouseMoveEvent(QMouseEvent *event) {
    if (zoneCreating_) {
        QPoint pt(event->pos());
        const double x = normalizePosition(pt.x(), width());
        const double y = normalizePosition(pt.y(), height());
        candidateZone_.width = x - candidateZone_.x;
        if (candidateZone_.type == Rectangle) {
            candidateZone_.height = y - candidateZone_.y;
        }
        else {
            candidateZone_.height = (height() > 0)
                ? candidateZone_.width * static_cast<double>(width()) / static_cast<double>(height())
                : candidateZone_.width;
        }
        updateOverlay();
    }
    
    GPURenderWidget::mouseMoveEvent(event);
}

void NokeyingZoneWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (zoneCreating_) {
        zoneCreating_ = false;
        QPoint pt(event->pos());
        const double x = normalizePosition(pt.x(), width());
        const double y = normalizePosition(pt.y(), height());
        candidateZone_.width = x - candidateZone_.x;
        if (candidateZone_.type == Rectangle) {
            candidateZone_.height = y - candidateZone_.y;
        }
        else {
            candidateZone_.height = (height() > 0)
                ? candidateZone_.width * static_cast<double>(width()) / static_cast<double>(height())
                : candidateZone_.width;
        }
        
        if (candidateZone_.isValid(size())) {
            ZoneData* z = new ZoneData(candidateZone_);
            zones_.append(z);
            emit zoneCreated(z->tag, z->type, z->x, z->y, z->width, z->height);
        }
        updateOverlay();
    }
    
    GPURenderWidget::mouseReleaseEvent(event);
}
