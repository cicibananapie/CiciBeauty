// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>
#include <vector>

#include "gpurenderwidget.h"

class LegZoneEditorWidget: public GPURenderWidget {
    Q_OBJECT
    
public:
    struct OriginalZoneInfo {
        int id = -1;
        float top = 0.0f;
        float bottom = 0.0f;
    };
    
public:
    LegZoneEditorWidget(GlobalModel *app, QWidget *parent = nullptr);
    ~LegZoneEditorWidget();
    
public:
    int addZone();
    bool removeZone(int id);
    void setZones(const std::vector<OriginalZoneInfo>& zones);
    std::vector<OriginalZoneInfo> allZones() const;
    void setSelectedZone(int id);
    int selectedZone() const;
    
private:
    enum DragType {
        NoDrag,
        UpBorder,
        DownBorder
    };
    
private:
    void updateZoneInfo(int zone, DragType dragType, int newYPosition);
    
private:
    EffectController::NativeRenderSourceType nativeRenderSourceType() const override;
    void paintOverlay(QPainter& painter) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    
signals:
    void zonesDirty();
    void zoneSelected(int id);
    void zoneDeleteRequested(int id);
    
private:
    //void json2Me(const nlohmann::json& j) override;
    
private:
    std::vector<OriginalZoneInfo> zones_;
    int idBase_ = 0;
    int selectedZoneId_ = -1;
    bool draging_ = false;
    DragType dragType_ = NoDrag;
    int dragZoneId_ = -1;
    bool hoverDeleteButton_ = false;
};

class LegPreviewWidget: public GPURenderWidget {
    Q_OBJECT
    
public:
    enum StretchDirection {
        StretchUp,
		StretchDown,
        StretchBiDirection
    };
    
    struct PreviewZoneInfo {
        int id = -1;
        float srcTop = 0.0f;
        float srcBottom = 0.0f;
        float dstTop = 0.0f;
        float dstBottom = 0.0f;
        StretchDirection direction = StretchBiDirection;
    };
    
public:
    LegPreviewWidget(GlobalModel *app, QWidget *parent = nullptr);
    ~LegPreviewWidget();
    
public:
    void setZones(const std::vector<PreviewZoneInfo>& zones);
    void clearZones();
    void setPreviewOffset(float offset);
    float previewOffset() const;
    void setSelectedZone(int id);
    int selectedZone() const;
    
private:
    void paintOverlay(QPainter& painter) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    
signals:
    void previewOffsetChangeRequested(float offset);
    
private:
    std::vector<PreviewZoneInfo> zones_;
    int selectedZoneId_ = -1;
    float previewOffset_ = 0.0f;
    bool dragingPreview_ = false;
    int dragStartY_ = 0;
    float dragStartOffset_ = 0.0f;
};
