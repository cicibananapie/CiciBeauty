// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>
#include <algorithm>
#include <cmath>

#include <QMouseEvent>
#include <QPainter>

#include "commonui.h"
#include "log.h"
#include "legwidget.h"
#include "../modelapp.h"

namespace {
constexpr int kLegZoneEditorMaxZones = 5;
constexpr int kLegZoneEditorBorderHitDistance = 6;
constexpr int kLegZoneEditorDeleteButtonSize = 20;
constexpr int kLegZoneEditorDeleteButtonMargin = 8;
constexpr float kLegZoneEditorEpsilon = 0.0001f;
constexpr float kLegZoneEditorMinZoneHeight = 0.02f;
constexpr bool kLegZoneEditorDebugLog = true;

float newZoneHeight(float availableHeight) {
	if (availableHeight < 2*kLegZoneEditorMinZoneHeight) {
		return -1.0f;
    }

    auto newHeight = availableHeight / 2;
    constexpr float maxHeight = 1.0f / 6;
    if (newHeight > maxHeight) {
	    newHeight = maxHeight;
    }

    return newHeight;
}

const char* legZoneDragTypeName(int dragType) {
    switch (dragType) {
        case 0:
            return "none";
        case 1:
            return "up-border";
        case 2:
            return "down-border";
        default:
            return "unknown";
    }
}

float clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    
    return value;
}

float normalizedY(int y, int height) {
    if (height <= 0) {
        return 0.0f;
    }
    
    return clampFloat(static_cast<float>(y) / height, 0.0f, 1.0f);
}

int yFromNormalized(float value, int height) {
    return static_cast<int>(clampFloat(value, 0.0f, 1.0f) * height + 0.5f);
}

qreal yFromPreviewPosition(float value, int height) {
    return static_cast<qreal>(value) * height;
}

float minZoneHeightRatio(int height) {
    if (height <= 0) {
        return 0.0f;
    }
    
    return kLegZoneEditorMinZoneHeight;
}

bool samePosition(float a, float b) {
    return std::fabs(a - b) < kLegZoneEditorEpsilon;
}

QRect deleteButtonRect(const LegZoneEditorWidget::OriginalZoneInfo& zone, const QSize& size) {
    const int left = size.width() - kLegZoneEditorDeleteButtonSize - kLegZoneEditorDeleteButtonMargin;
    const int top = yFromNormalized(zone.top, size.height()) + kLegZoneEditorDeleteButtonMargin;
    return QRect(left, top,
                 kLegZoneEditorDeleteButtonSize, kLegZoneEditorDeleteButtonSize);
}

}

LegZoneEditorWidget::LegZoneEditorWidget(GlobalModel *app, QWidget *parent /* = nullptr*/)
	: GPURenderWidget(app, parent) {
    setMouseTracking(true);
}

LegZoneEditorWidget::~LegZoneEditorWidget() {
}

int LegZoneEditorWidget::addZone() {
    if (zones_.size() >= kLegZoneEditorMaxZones) {
        if (kLegZoneEditorDebugLog) {
            LogWarn << "[LegZoneEditor] addZone failed reason=max-zones zoneCount="
                    << zones_.size() << logEnd;
        }
        return -1;
    }
    
    const float minHeight = minZoneHeightRatio(height());
    if (minHeight <= 0.0f) {
        if (kLegZoneEditorDebugLog) {
            LogWarn << "[LegZoneEditor] addZone failed reason=invalid-min-height widget="
                    << width() << "x" << height() << logEnd;
        }
        return -1;
    }
    
    const float availableBottom = zones_.empty() ? 1.0f : zones_.front().top;
    const float availableHeight = availableBottom;
    
    const float zoneHeight = newZoneHeight(availableHeight);
    if (zoneHeight < 0) {
	    if (kLegZoneEditorDebugLog) {
		    LogWarn << "[LegZoneEditor] addZone failed reason=no-space availableHeight=" << availableHeight
			    << " minHeight=" << minHeight << " zoneCount=" << zones_.size() << logEnd;
	    }
	    return -1;
    }

    OriginalZoneInfo z;
    z.id = idBase_++;
    z.bottom = availableBottom;
    z.top = z.bottom - zoneHeight;
    zones_.insert(zones_.begin(), z);
    if (kLegZoneEditorDebugLog) {
        LogInfo << "[LegZoneEditor] addZone id=" << z.id
                << " top=" << z.top
                << " bottom=" << z.bottom
                << " zoneHeight=" << zoneHeight
                << " widget=" << width() << "x" << height()
                << " zoneCount=" << zones_.size() << logEnd;
    }
    emit zonesDirty();
    updateOverlay();
    
    return z.id;
}

bool LegZoneEditorWidget::removeZone(int id) {
    const auto it = std::find_if(zones_.begin(), zones_.end(), [id](const auto& zone) {
        return zone.id == id;
    });
    if (it == zones_.end()) {
        if (kLegZoneEditorDebugLog) {
            LogWarn << "[LegZoneEditor] removeZone failed id=" << id
                    << " zoneCount=" << zones_.size() << logEnd;
        }
        return false;
    }
    
    if (kLegZoneEditorDebugLog) {
        LogInfo << "[LegZoneEditor] removeZone id=" << id
                << " top=" << it->top
                << " bottom=" << it->bottom
                << " zoneCountBefore=" << zones_.size() << logEnd;
    }
    zones_.erase(it);
    if (selectedZoneId_ == id) {
        selectedZoneId_ = -1;
        hoverDeleteButton_ = false;
        emit zoneSelected(selectedZoneId_);
    }
    if (dragZoneId_ == id) {
        draging_ = false;
        dragType_ = NoDrag;
        dragZoneId_ = -1;
    }
    
    emit zonesDirty();
    updateOverlay();
    
    return true;
}

void LegZoneEditorWidget::setZones(const std::vector<OriginalZoneInfo>& zones) {
    zones_ = zones;
    
    int maxId = -1;
    for (const auto& zone: zones_) {
        maxId = std::max(maxId, zone.id);
    }
    idBase_ = std::max(idBase_, maxId + 1);
    if (selectedZoneId_ != -1) {
        const auto it = std::find_if(zones_.begin(), zones_.end(), [this](const auto& zone) {
            return zone.id == selectedZoneId_;
        });
        if (it == zones_.end()) {
            selectedZoneId_ = -1;
            hoverDeleteButton_ = false;
            emit zoneSelected(selectedZoneId_);
        }
    }
    
    if (kLegZoneEditorDebugLog) {
        LogInfo << "[LegZoneEditor] setZones zoneCount=" << zones_.size()
                << " selectedZoneId=" << selectedZoneId_
                << " idBase=" << idBase_ << logEnd;
        for (int i = 0; i < static_cast<int>(zones_.size()); i++) {
            const auto& zone = zones_[i];
            LogInfo << "[LegZoneEditor]   zone slot=" << i
                    << " id=" << zone.id
                    << " top=" << zone.top
                    << " bottom=" << zone.bottom << logEnd;
        }
    }
    emit zonesDirty();
    updateOverlay();
}

std::vector<LegZoneEditorWidget::OriginalZoneInfo> LegZoneEditorWidget::allZones() const {
    return zones_;
}

void LegZoneEditorWidget::setSelectedZone(int id) {
    if (id != -1) {
        const auto it = std::find_if(zones_.begin(), zones_.end(), [id](const auto& zone) {
            return zone.id == id;
        });
        if (it == zones_.end()) {
            id = -1;
        }
    }
    
    if (selectedZoneId_ == id) {
        return;
    }
    
    selectedZoneId_ = id;
    hoverDeleteButton_ = false;
    emit zoneSelected(selectedZoneId_);
    updateOverlay();
}

int LegZoneEditorWidget::selectedZone() const {
    return selectedZoneId_;
}

void LegZoneEditorWidget::updateZoneInfo(int zone, DragType dragType, int newYPosition) {
    auto it = std::find_if(zones_.begin(), zones_.end(), [zone](const auto& item) {
        return item.id == zone;
    });
    if (it == zones_.end()) {
        if (kLegZoneEditorDebugLog) {
            LogWarn << "[LegZoneEditor] updateZoneInfo failed zoneId=" << zone
                    << " dragType=" << legZoneDragTypeName(dragType)
                    << " y=" << newYPosition
                    << " zoneCount=" << zones_.size() << logEnd;
        }
        return;
    }
    
    const float minHeight = minZoneHeightRatio(height());
    const float position = normalizedY(newYPosition, height());
    const auto index = static_cast<int>(std::distance(zones_.begin(), it));
    bool changed = false;
    
    if (dragType == UpBorder) {
        float minTop = 0.0f;
        const float maxTop = it->bottom - minHeight;
        if (index > 0) {
            assert(samePosition(zones_[index - 1].bottom, it->top));
            minTop = zones_[index - 1].top + minHeight;
        }
        if (minTop <= maxTop) {
            const float top = clampFloat(position, minTop, maxTop);
            if (!samePosition(top, it->top)) {
                if (index > 0) {
                    zones_[index - 1].bottom = top;
                }
                it->top = top;
                changed = true;
            }
        }
    }
    else if (dragType == DownBorder) {
        float maxBottom = 1.0f;
        const float minBottom = it->top + minHeight;
        if (index + 1 < static_cast<int>(zones_.size())) {
            assert(samePosition(zones_[index + 1].top, it->bottom));
            maxBottom = zones_[index + 1].bottom - minHeight;
        }
        if (minBottom <= maxBottom) {
            const float bottom = clampFloat(position, minBottom, maxBottom);
            if (!samePosition(bottom, it->bottom)) {
                if (index + 1 < static_cast<int>(zones_.size())) {
                    zones_[index + 1].top = bottom;
                }
                it->bottom = bottom;
                changed = true;
            }
        }
    }
    
    if (changed) {
        if (kLegZoneEditorDebugLog) {
            LogInfo << "[LegZoneEditor] updateZoneInfo zoneId=" << zone
                    << " dragType=" << legZoneDragTypeName(dragType)
                    << " y=" << newYPosition
                    << " normalized=" << position
                    << " minHeight=" << minHeight
                    << " top=" << it->top
                    << " bottom=" << it->bottom
                    << " index=" << index << logEnd;
        }
        emit zonesDirty();
        updateOverlay();
    }
}

GPURenderWidget::NativeRenderSourceType LegZoneEditorWidget::nativeRenderSourceType() const {
    return NativeRenderSourceType::Input;
}

void LegZoneEditorWidget::paintOverlay(QPainter& painter) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    const QColor selectedFillColor(0, 180, 255, 42); // translucent cyan-blue selected zone fill
    const QColor selectedLineColor(0, 210, 255, 235); // bright cyan selected zone line
    const QColor normalLineColor(255, 255, 255, 165); // translucent white normal zone line
    const QColor deleteButtonFillColor(24, 28, 36, 210); // dark slate delete button fill
    const QColor deleteButtonHoverFillColor(225, 55, 70, 225); // vivid red delete button hover fill
    const QColor deleteButtonBorderColor(255, 255, 255, 100); // translucent white delete button border
    const QColor deleteButtonIconColor(255, 255, 255, 235); // bright white delete button icon
    
    for (const auto& zone: zones_) {
        const int top = yFromNormalized(zone.top, height());
        const int bottom = yFromNormalized(zone.bottom, height());
        if (zone.id == selectedZoneId_) {
            painter.fillRect(QRect(0, top, width(), bottom - top), selectedFillColor);
        }
        
        const bool selected = zone.id == selectedZoneId_;
        painter.setPen(QPen(selected ? selectedLineColor : normalLineColor, 1));
        painter.drawLine(QPoint(0, top), QPoint(width(), top));
        painter.drawLine(QPoint(0, bottom), QPoint(width(), bottom));

        if (selected) {
            const QRect rc = deleteButtonRect(zone, size());
            painter.setPen(QPen(deleteButtonBorderColor, 1));
            painter.setBrush(hoverDeleteButton_ ? deleteButtonHoverFillColor : deleteButtonFillColor);
            painter.drawEllipse(rc);
            painter.setPen(QPen(deleteButtonIconColor, 2));
            painter.drawLine(rc.topLeft() + QPoint(6, 6), rc.bottomRight() - QPoint(6, 6));
            painter.drawLine(rc.topRight() + QPoint(-6, 6), rc.bottomLeft() + QPoint(6, -6));
        }
    }
}

void LegZoneEditorWidget::mousePressEvent(QMouseEvent *event) {
    const QPoint pt = event->pos();
    
    const auto selectedIt = std::find_if(zones_.begin(), zones_.end(), [this](const auto& zone) {
        return zone.id == selectedZoneId_;
    });
    if (selectedIt != zones_.end() && deleteButtonRect(*selectedIt, size()).contains(pt)) {
        emit zoneDeleteRequested(selectedIt->id);
        event->accept();
        return;
    }
    
    for (const auto& zone: zones_) {
        const int top = yFromNormalized(zone.top, height());
        const int bottom = yFromNormalized(zone.bottom, height());
        if (std::abs(pt.y() - top) <= kLegZoneEditorBorderHitDistance) {
            setSelectedZone(zone.id);
            draging_ = true;
            dragType_ = UpBorder;
            dragZoneId_ = zone.id;
            event->accept();
            return;
        }
        if (std::abs(pt.y() - bottom) <= kLegZoneEditorBorderHitDistance) {
            setSelectedZone(zone.id);
            draging_ = true;
            dragType_ = DownBorder;
            dragZoneId_ = zone.id;
            event->accept();
            return;
        }
        if (pt.y() >= top && pt.y() <= bottom) {
            setSelectedZone(zone.id);
            event->accept();
            return;
        }
    }
    
    setSelectedZone(-1);
    GPURenderWidget::mousePressEvent(event);
}

void LegZoneEditorWidget::mouseMoveEvent(QMouseEvent *event) {
    const QPoint pt = event->pos();
    
    if (draging_) {
        if (hoverDeleteButton_) {
            hoverDeleteButton_ = false;
            updateOverlay();
        }
        updateZoneInfo(dragZoneId_, dragType_, pt.y());
        event->accept();
        return;
    }
    
    bool onHotArea = false;
    bool hoverDeleteButton = false;
    const auto selectedIt = std::find_if(zones_.begin(), zones_.end(), [this](const auto& zone) {
        return zone.id == selectedZoneId_;
    });
    if (selectedIt != zones_.end() && deleteButtonRect(*selectedIt, size()).contains(pt)) {
        setCursor(Qt::PointingHandCursor);
        onHotArea = true;
        hoverDeleteButton = true;
    }
    else {
        for (const auto& zone: zones_) {
            const int top = yFromNormalized(zone.top, height());
            const int bottom = yFromNormalized(zone.bottom, height());
            if (std::abs(pt.y() - top) <= kLegZoneEditorBorderHitDistance
                || std::abs(pt.y() - bottom) <= kLegZoneEditorBorderHitDistance) {
                setCursor(Qt::SplitVCursor);
                onHotArea = true;
                break;
            }
        }
    }
    
    if (!onHotArea) {
        setCursor(Qt::ArrowCursor);
    }
    if (hoverDeleteButton_ != hoverDeleteButton) {
        hoverDeleteButton_ = hoverDeleteButton;
        updateOverlay();
    }
    
    GPURenderWidget::mouseMoveEvent(event);
}

void LegZoneEditorWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (draging_) {
        draging_ = false;
        dragType_ = NoDrag;
        dragZoneId_ = -1;
        event->accept();
        return;
    }
    
    GPURenderWidget::mouseReleaseEvent(event);
}

LegPreviewWidget::LegPreviewWidget(GlobalModel *app, QWidget *parent /* = nullptr*/) : GPURenderWidget(app, parent) {
    setMouseTracking(true);
}

LegPreviewWidget::~LegPreviewWidget() {
}

void LegPreviewWidget::setZones(const std::vector<PreviewZoneInfo>& zones) {
    zones_ = zones;
    if (selectedZoneId_ != -1) {
        const auto it = std::find_if(zones_.begin(), zones_.end(), [this](const auto& zone) {
            return zone.id == selectedZoneId_;
        });
        if (it == zones_.end()) {
            selectedZoneId_ = -1;
        }
    }
    
    updateOverlay();
}

void LegPreviewWidget::clearZones() {
    zones_.clear();
    selectedZoneId_ = -1;
    updateOverlay();
}

void LegPreviewWidget::setPreviewOffset(float offset) {
    (void)offset;
    // Keep overlay in effect-output coordinates until the MRE package supports viewport offset.
    previewOffset_ = 0.0f;
    updateOverlay();
}

float LegPreviewWidget::previewOffset() const {
    return previewOffset_;
}

void LegPreviewWidget::setSelectedZone(int id) {
    if (id != -1) {
        const auto it = std::find_if(zones_.begin(), zones_.end(), [id](const auto& zone) {
            return zone.id == id;
        });
        if (it == zones_.end()) {
            id = -1;
        }
    }
    
    if (selectedZoneId_ == id) {
        return;
    }
    
    selectedZoneId_ = id;
    updateOverlay();
}

int LegPreviewWidget::selectedZone() const {
    return selectedZoneId_;
}

void LegPreviewWidget::paintOverlay(QPainter& painter) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    const QColor selectedFillColor(0, 180, 255, 50); // translucent cyan-blue selected preview fill
    const QColor selectedLineColor(0, 210, 255, 235); // bright cyan selected preview line
    const QColor normalLineColor(255, 255, 255, 145); // translucent white normal preview line
    
    for (const auto& zone: zones_) {
        const qreal top = yFromPreviewPosition(zone.dstTop - previewOffset_, height());
        const qreal bottom = yFromPreviewPosition(zone.dstBottom - previewOffset_, height());
        const bool selected = zone.id == selectedZoneId_;
        if (selected) {
            painter.fillRect(QRectF(0.0, top, width(), bottom - top), selectedFillColor);
        }
        
        painter.setPen(QPen(selected ? selectedLineColor : normalLineColor,1));
        painter.drawLine(QPointF(0.0, top), QPointF(width(), top));
        painter.drawLine(QPointF(0.0, bottom), QPointF(width(), bottom));
    }
}

void LegPreviewWidget::mousePressEvent(QMouseEvent *event) {
    dragingPreview_ = true;
    dragStartY_ = event->pos().y();
    dragStartOffset_ = previewOffset_;
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void LegPreviewWidget::mouseMoveEvent(QMouseEvent *event) {
    if (!dragingPreview_) {
        setCursor(Qt::OpenHandCursor);
        GPURenderWidget::mouseMoveEvent(event);
        return;
    }
    
    if (height() > 0) {
        // Viewport offset is disabled until the MRE package supports it.
        // const float delta = static_cast<float>(event->pos().y() - dragStartY_) / height();
        // emit previewOffsetChangeRequested(dragStartOffset_ - delta);
    }
    event->accept();
}

void LegPreviewWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (dragingPreview_) {
        dragingPreview_ = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    
    GPURenderWidget::mouseReleaseEvent(event);
}
