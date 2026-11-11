// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QList>
#include <QMetaObject>
#include <QWidget>
#include <functional>
#include <memory>

#include "protocol/preview.h"
#include "../modelapp.h"

class QPainter;
namespace cici::protocol {
struct JsonMessage;
}
class GPURenderWidget : public QWidget, private IFilterModelChangeObserver {
    Q_OBJECT

public:
    GPURenderWidget(GlobalModel *app, QWidget *parent = nullptr);
    ~GPURenderWidget() override;
    static void autoKeying(GlobalModel* app, QObject* owner, std::function<void(uint32_t, float)> callback);
    
protected:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    using NativeRenderSourceType = cici::protocol::PreviewSource;
    virtual NativeRenderSourceType nativeRenderSourceType() const;
    virtual void paintOverlay(QPainter& painter);
    void updateOverlay();
    void updateColorPickMagnifier(int x, int y, float radius, float zoomRatio, bool active);
    void requestColorPick(int x, int y, std::function<void(uint32_t)> callback);

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
    void onPreviewMessage(const cici::protocol::JsonMessage& message);
    void consumePreview(const cici::protocol::PreviewFrame& frame);

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
    struct Preview;
    std::unique_ptr<Preview> preview_;
    std::u8string previewId_;
    std::u8string instanceId_;
    bool dpiRefreshPending_ = false;
};
