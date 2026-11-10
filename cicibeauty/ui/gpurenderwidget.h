// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QList>
#include <QMetaObject>
#include <QWidget>

#include "../effect/effectcontroller.h"
#include "../modelapp.h"

class QPainter;
class GPURenderWidget : public QWidget, private IFilterModelChangeObserver {
    Q_OBJECT

public:
    GPURenderWidget(GlobalModel *app, QWidget *parent = nullptr);
    ~GPURenderWidget() override;
    
protected:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    virtual EffectController::NativeRenderSourceType nativeRenderSourceType() const;
    virtual void paintOverlay(QPainter& painter);
    void updateOverlay();

private:
    void onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) override;
    void registerNativeWindow();
    void unregisterNativeWindow();
    void resizeNativeWindow(int w, int h);
    QSize nativePixelSize() const;
    void updateNativeWindowGeometry();
    void updateOverlayWindowFilter();
    void clearOverlayWindowFilter();
    void createNativeWindow();
    void destroyNativeWindow();

private:
    class OverlayWidget;
    friend class OverlayWidget;

protected:
    GlobalModel *app_ = nullptr;

private:
    OverlayWidget* overlay_ = nullptr;
    QList<QObject*> overlayEventTargets_;
    QList<QMetaObject::Connection> overlayScrollConnections_;
    void* nativeHandle_ = nullptr;   //windows:HWND, macos:NSView*
    FilterModel* nativeRenderModel_ = nullptr;
    EffectController::NativeRenderTargetId nativeRenderId_ = 0;
    bool dpiRefreshPending_ = false;
};
