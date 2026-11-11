// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>

#include <QCursor>
#include <QWidget>
#include <QPoint>

#include "gpurenderwidget.h"

class GlobalModel;

class KeyingColorPickWidget : public GPURenderWidget {
    Q_OBJECT
    
public:
    KeyingColorPickWidget(GlobalModel *app, QWidget *parent = nullptr);
    ~KeyingColorPickWidget();
    QSize sizeHint() const override;
    
private:
    virtual void enterEvent(QEnterEvent *event) override;
    virtual void leaveEvent(QEvent *event) override;
    virtual void mouseMoveEvent(QMouseEvent *event) override;
    virtual void mousePressEvent(QMouseEvent *event) override;
    
private:
    NativeRenderSourceType nativeRenderSourceType() const override;
    void paintOverlay(QPainter& painter) override;
    void updateMagnifier(const QPoint& pos, bool active);
    
    
signals:
    void colorPicked(unsigned int);
    
private:
    QCursor oldCursor_;
    QPoint pos_;
};
