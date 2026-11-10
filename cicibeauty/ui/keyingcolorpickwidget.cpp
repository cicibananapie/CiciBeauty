// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>

#include <QPainter>
#include <QRegion>
#include <QColor>
#include <QMouseEvent>
#include <QPointer>

#include "log.h"
#include "filtermodelhelpers.h"
#include "keyingcolorpickwidget.h"
#include "../effect/effectcontroller.h"
#include "../modelapp.h"

namespace {
constexpr int kDefaultColorPickWidth = 270;
constexpr int kDefaultColorPickHeight = 480;
constexpr int kMagnifierLogicalSize = 170;
constexpr float kMagnifierZoomRatio = 10.0f;
}

KeyingColorPickWidget::KeyingColorPickWidget(GlobalModel *app, QWidget *parent /* = nullptr*/)
    : GPURenderWidget(app, parent) {
    
}

KeyingColorPickWidget::~KeyingColorPickWidget() {
    
}

QSize KeyingColorPickWidget::sizeHint() const {
    return QSize(kDefaultColorPickWidth, kDefaultColorPickHeight);
}

void KeyingColorPickWidget::enterEvent(QEnterEvent *event) {
	LogTrace << "KeyingColorPickWidget::enterEvent" << logEnd;
    oldCursor_ = cursor();
	setCursor(Qt::CrossCursor);
    updateMagnifier(mapFromGlobal(QCursor::pos()), true);
    
    GPURenderWidget::enterEvent(event);
}

void KeyingColorPickWidget::leaveEvent(QEvent *event) {
	LogTrace << "KeyingColorPickWidget::leaveEvent" << logEnd;
    setCursor(oldCursor_);
    updateMagnifier(QPoint(), false);
    
    GPURenderWidget::leaveEvent(event);
}

void KeyingColorPickWidget::mouseMoveEvent(QMouseEvent *event) {
	LogTrace << "KeyingColorPickWidget::mouseMoveEvent" << logEnd;
    updateMagnifier(event->pos(), true);
	GPURenderWidget::mouseMoveEvent(event);
}

void KeyingColorPickWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        return ;
    }
    QPointer<KeyingColorPickWidget> self(this);
    currentEffectController(app_)->requestColorPick([self](uint32_t rgba) {
        if (!self) {
            return;
        }
        emit self->colorPicked(rgba);
    });
    
    GPURenderWidget::mousePressEvent(event);
}

EffectController::NativeRenderSourceType KeyingColorPickWidget::nativeRenderSourceType() const {
	return EffectController::InputWithMagnifier;
}

void KeyingColorPickWidget::updateMagnifier(const QPoint& pos, bool active) {
    pos_ = pos;

    if (!active) {
        currentEffectController(app_)->updateColorPickMagnifier(0, 0, 0, 1.0f, false);
        return;
    }

    const qreal dpr = devicePixelRatioF();
    const int previewX = static_cast<int>(pos.x() * dpr + 0.5);
    const int previewY = static_cast<int>(pos.y() * dpr + 0.5);
    const int magnifierW = static_cast<int>(kMagnifierLogicalSize * dpr + 0.5);
    const float magnifierRadius = static_cast<float>(magnifierW) * 0.5f;
    LogTrace << "updateMagnifier center:" << previewX << ", " << previewY
             << " radius(" << magnifierRadius << ")" << logEnd;
    currentEffectController(app_)->updateColorPickMagnifier(previewX,
                                                previewY,
                                                magnifierRadius,
                                                kMagnifierZoomRatio,
                                                true);
}

void KeyingColorPickWidget::paintOverlay(QPainter& painter) {
	(void)painter;
	/*const QPoint pt = pos_;
    int x = pt.x() - 5;
    int y = pt.y() - 5;
    painter.setPen(QColor(255, 0, 0));
    painter.drawLine(x, y, x + 10, y);
    painter.drawLine(x + 10, y, x + 10, y + 10);
    painter.drawLine(x + 10, y + 10, x, y + 10);
    painter.drawLine(x, y + 10, x, y);*/
}
