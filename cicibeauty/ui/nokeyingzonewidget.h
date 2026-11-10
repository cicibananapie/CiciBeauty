// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include "gpurenderwidget.h"

class NokeyingZoneWidget : public GPURenderWidget {
    Q_OBJECT
    
public:
    enum ZoneType {
        Rectangle,
        Circle
    };
    
    struct ZoneData {
        ZoneData();
        ZoneData(const ZoneData& r);
        bool isValid(const QSize& canvasSize) const;
        bool contain(const QPoint& pt, const QSize& canvasSize) const;
        void reset();
        QRect toPixelRect(const QSize& canvasSize) const;
        
        QString tag;
        double x;
        double y;
        double width;
        double height;
        ZoneType type;
    };
    
public:
    NokeyingZoneWidget(GlobalModel *app, QWidget *parent = nullptr);
    ~NokeyingZoneWidget();
    
public:
    void setZoneType(ZoneType t) {typeUsed_ = t;}
    void setZones(const QList<ZoneData>& zones);
    void clearZoneData();
    
private:
    EffectController::NativeRenderSourceType nativeRenderSourceType() const override;
    void paintOverlay(QPainter& painter) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    
signals:
    void zoneCreated(const QString&, int, double, double, double, double);
    void zoneDeleted(const QString&);
    
private:
    QList<ZoneData*> zones_;
    bool zoneCreating_ = false;
    ZoneData candidateZone_;
    ZoneData* selectedZone_ = nullptr;
    ZoneType typeUsed_ = Rectangle;
};
