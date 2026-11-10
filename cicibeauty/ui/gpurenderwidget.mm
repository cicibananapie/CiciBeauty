// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "gpurenderwidget.h"
#if CICI_OS_MACOS
#import <AppKit/AppKit.h>
#elif CICI_OS_WINDOWS
#include <Windows.h>
#else
#error "Unsupported platform"
#endif

#include <QAbstractScrollArea>
#include <QEnterEvent>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QRegion>
#include <QScrollBar>
#include <QWheelEvent>

#include "log.h"
#include "cicibase.h"
#include "../modelapp.h"

#if CICI_OS_MACOS
void attachMacOverlayWindow(void* ownerViewHandle, void* overlayViewHandle) {
    NSView* ownerView = (__bridge NSView*)ownerViewHandle;
    NSView* overlayView = (__bridge NSView*)overlayViewHandle;
    NSWindow* ownerWindow = ownerView.window;
    NSWindow* overlayWindow = overlayView.window;
    if (ownerWindow == nil || overlayWindow == nil || ownerWindow == overlayWindow) {
        return;
    }

    overlayWindow.hasShadow = NO;
    overlayWindow.level = ownerWindow.level;
    if (overlayWindow.parentWindow == ownerWindow) {
        return;
    }

    [overlayWindow.parentWindow removeChildWindow:overlayWindow];
    [ownerWindow addChildWindow:overlayWindow ordered:NSWindowAbove];
}
#endif

QSize scaledPreviewSizeFromRenderSize(int w, int h) {
	if (w <= 0 || h <= 0) {
		return QSize();
	}

	int *pMax = (w > h) ? &w : &h;
	while (*pMax > 800) {
		w /= 2;
		h /= 2;
	}

	return QSize(w, h);
}

#if CICI_OS_MACOS
void GPURenderWidget::createNativeWindow() {
	if (nativeHandle_ != nullptr) {
		return;
	}

	nativeHandle_ = reinterpret_cast<void*>(winId());
	assert(nativeHandle_ != nullptr);
}

void GPURenderWidget::destroyNativeWindow() {
	// The native NSView is owned by Qt; GPURenderWidget only borrows its handle.
	nativeHandle_ = nullptr;
}

#elif CICI_OS_WINDOWS
void GPURenderWidget::createNativeWindow() {
    if (nativeHandle_ != nullptr) {
        return;
    }

    HINSTANCE instance = ::GetModuleHandleW(nullptr);
    HWND parentHwnd = reinterpret_cast<HWND>(winId());
    assert(parentHwnd != nullptr);

	HWND nativeHwnd = ::CreateWindowExW(0,
                                        L"STATIC",
                                        L"EffectRenderArea",
                                        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                        0, 0, 100, 100,
                                        parentHwnd,
                                        nullptr,
                                        instance,
                                        nullptr);
    assert(nativeHwnd != nullptr);
    ::ShowWindow(nativeHwnd, SW_HIDE);

	nativeHandle_ = nativeHwnd;
}

void GPURenderWidget::destroyNativeWindow() {
    if (nativeHandle_ != nullptr) {
        ::DestroyWindow(reinterpret_cast<HWND>(nativeHandle_));
        nativeHandle_ = nullptr;
    }
}
#else
#error "Unsupported platform"
#endif

class GPURenderWidget::OverlayWidget : public QWidget {
public:
    explicit OverlayWidget(GPURenderWidget* owner)
        : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint)
        , owner_(owner) {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
	    //setAttribute(Qt::WA_TransparentForMouseEvents, true);
	    setAutoFillBackground(false);
	    setMouseTracking(true);
    }

    void attachToOwnerWindow() {
#if CICI_OS_MACOS
        attachMacOverlayWindow(reinterpret_cast<void*>(owner_->winId()),
                               reinterpret_cast<void*>(winId()));
#endif
    }

    void reposition() {
        if (!owner_) {
            return;
        }

        const QRect ownerGlobalRect(owner_->mapToGlobal(QPoint(0, 0)), owner_->size());
        QRect visibleGlobalRect = ownerGlobalRect;
        for (QWidget* widget = owner_; widget != nullptr; widget = widget->parentWidget()) {
            if (!widget->isVisible()) {
                visibleGlobalRect = QRect();
                break;
            }

            const QRect widgetGlobalRect(widget->mapToGlobal(QPoint(0, 0)), widget->size());
            visibleGlobalRect = visibleGlobalRect.intersected(widgetGlobalRect);
            if (visibleGlobalRect.isEmpty()) {
                break;
            }
        }

        setGeometry(ownerGlobalRect);
        if (visibleGlobalRect.isEmpty()) {
            setMask(QRegion());
            return;
        }

        const QRect ownerVisibleRect(visibleGlobalRect.topLeft() - ownerGlobalRect.topLeft(),
                                     visibleGlobalRect.size());
        if (ownerVisibleRect == owner_->rect()) {
            clearMask();
        } else {
            setMask(QRegion(ownerVisibleRect));
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.fillRect(rect(), Qt::transparent);
        owner_->paintOverlay(painter);
    }

    void mousePressEvent(QMouseEvent *event) override { 
        owner_->mousePressEvent(event);
	    QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override {
	    owner_->mouseMoveEvent(event);
	    QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
	    owner_->mouseReleaseEvent(event);
	    QWidget::mouseReleaseEvent(event);
    }

private:
    GPURenderWidget* owner_ = nullptr;
};

GPURenderWidget::GPURenderWidget(GlobalModel *app, QWidget *parent)
    : QWidget(parent)
    , app_(app) {
    assert(app_ != nullptr);
    app_->registerFilterModelChangeObserver(this);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);

    overlay_ = new OverlayWidget(this);
    overlay_->setGeometry(rect());
    overlay_->raise();

#if CICI_OS_WINDOWS
    (void)winId();
#endif
    updateOverlayWindowFilter();
}

GPURenderWidget::~GPURenderWidget() {
    clearOverlayWindowFilter();
	unregisterNativeWindow();
    app_->unregisterFilterModelChangeObserver(this);
}

bool GPURenderWidget::event(QEvent* event) {
    const bool handled = QWidget::event(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const bool devicePixelRatioChanged = event->type() == QEvent::DevicePixelRatioChange;
#else
    const bool devicePixelRatioChanged = event->type() == QEvent::ScreenChangeInternal;
#endif
    if (!devicePixelRatioChanged || dpiRefreshPending_) {
        return handled;
    }

    dpiRefreshPending_ = true;
    QPointer<GPURenderWidget> self(this);
    QMetaObject::invokeMethod(this, [self]() {
        if (!self) {
            return;
        }

        self->dpiRefreshPending_ = false;
        if (!self->isVisible()) {
            return;
        }

        LogInfo << "[KeyingPreview] device pixel ratio changed widget=" << self.data()
                << ", dpr=" << self->devicePixelRatioF() << logEnd;
        self->unregisterNativeWindow();
        self->registerNativeWindow();
    }, Qt::QueuedConnection);
    return handled;
}

void GPURenderWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateNativeWindowGeometry();
    overlay_->reposition();
    overlay_->raise();
}

void GPURenderWidget::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    overlay_->reposition();
    overlay_->raise();
}

void GPURenderWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    unregisterNativeWindow();
    if (overlay_ != nullptr) {
        overlay_->hide();
    }
#if CICI_OS_WINDOWS
    if (nativeHandle_ != nullptr) {
        ::ShowWindow(reinterpret_cast<HWND>(nativeHandle_), SW_HIDE);
    }
#endif
}

void GPURenderWidget::showEvent(QShowEvent* event) {
	QWidget::showEvent(event);
    updateOverlayWindowFilter();
	registerNativeWindow();
    overlay_->reposition();
    overlay_->show();
    overlay_->attachToOwnerWindow();
    overlay_->raise();
}

bool GPURenderWidget::eventFilter(QObject* watched, QEvent* event) {
    const bool watchedOverlayTarget = overlayEventTargets_.contains(watched);
    const bool needReposition = event->type() == QEvent::Move ||
                                event->type() == QEvent::Resize ||
                                event->type() == QEvent::Show ||
                                event->type() == QEvent::Hide ||
                                event->type() == QEvent::LayoutRequest ||
                                event->type() == QEvent::Wheel;
    if (watchedOverlayTarget && needReposition && overlay_ != nullptr) {
        overlay_->reposition();
        overlay_->raise();
    }
    return QWidget::eventFilter(watched, event);
}

void GPURenderWidget::updateOverlayWindowFilter() {
    clearOverlayWindowFilter();

    auto addTarget = [this](QObject* target) {
        if (target == nullptr || overlayEventTargets_.contains(target)) {
            return;
        }
        target->installEventFilter(this);
        overlayEventTargets_.append(target);
    };

    for (QWidget* widget = this; widget != nullptr; widget = widget->parentWidget()) {
        addTarget(widget);

        auto* scrollArea = qobject_cast<QAbstractScrollArea*>(widget);
        if (scrollArea == nullptr) {
            continue;
        }

        addTarget(scrollArea->viewport());

        QScrollBar* verticalScrollBar = scrollArea->verticalScrollBar();
        if (verticalScrollBar != nullptr) {
            addTarget(verticalScrollBar);
            overlayScrollConnections_.append(
                connect(verticalScrollBar, &QScrollBar::valueChanged, this, [this](int) {
                    if (overlay_ != nullptr) {
                        overlay_->reposition();
                        overlay_->raise();
                    }
                }));
        }

        QScrollBar* horizontalScrollBar = scrollArea->horizontalScrollBar();
        if (horizontalScrollBar != nullptr) {
            addTarget(horizontalScrollBar);
            overlayScrollConnections_.append(
                connect(horizontalScrollBar, &QScrollBar::valueChanged, this, [this](int) {
                    if (overlay_ != nullptr) {
                        overlay_->reposition();
                        overlay_->raise();
                    }
                }));
        }
    }
}

void GPURenderWidget::clearOverlayWindowFilter() {
    for (const QMetaObject::Connection& connection : overlayScrollConnections_) {
        disconnect(connection);
    }
    overlayScrollConnections_.clear();

    for (QObject* target : overlayEventTargets_) {
        if (target != nullptr) {
            target->removeEventFilter(this);
        }
    }
    overlayEventTargets_.clear();
}

EffectController::NativeRenderSourceType GPURenderWidget::nativeRenderSourceType() const {
	return EffectController::Output;
}

void GPURenderWidget::paintOverlay(QPainter& painter) {
    (void)painter;
}

void GPURenderWidget::updateOverlay() {
    if (overlay_ != nullptr) {
        overlay_->update();
    }
}

QSize GPURenderWidget::nativePixelSize() const {
    const qreal dpr = devicePixelRatioF();
    return QSize(qRound(width() * dpr), qRound(height() * dpr));
}

void GPURenderWidget::updateNativeWindowGeometry() {
#if CICI_OS_WINDOWS
    if (nativeHandle_ == nullptr) {
        return;
    }

    HWND nativeHwnd = reinterpret_cast<HWND>(nativeHandle_);
    const QSize pixelSize = nativePixelSize();
    ::SetWindowPos(nativeHwnd,
                   HWND_TOP,
                   0,
                   0,
                   pixelSize.width(),
                   pixelSize.height(),
                   SWP_NOACTIVATE);
#endif
}

void GPURenderWidget::onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) {
    assert(isMainThread());
    if (nativeRenderModel_ == oldModel) {
        unregisterNativeWindow();
    }

    if (isVisible() && newModel != nullptr && nativeRenderId_ == 0) {
        registerNativeWindow();
    }
}

void GPURenderWidget::registerNativeWindow() {
	assert(isMainThread());
	if (nativeRenderId_ != 0) {
		return;
	}

	auto* model = app_->filterModel();
	if (model == nullptr) {
		return;
	}

	auto* effectCtrl = model->effectController();
	if (effectCtrl == nullptr) {
		return;
	}

	int w = 0;
	int h = 0;
	effectCtrl->getRenderSize(w, h);
	const QSize previewSize = scaledPreviewSizeFromRenderSize(w, h);
	if (previewSize.isEmpty()) {
		LogWarn << "[KeyingPreview] registration skipped widget=" << this
		        << ", controller=" << effectCtrl
		        << ", renderSize=" << w << "x" << h << logEnd;
		return;
	}

	setFixedSize(previewSize);
	createNativeWindow();
	const QSize targetSize = nativePixelSize();
	LogInfo << "[KeyingPreview] register widget=" << this
	        << ", controller=" << effectCtrl
	        << ", renderSize=" << w << "x" << h
	        << ", previewSize=" << previewSize.width() << "x" << previewSize.height()
	        << ", targetSize=" << targetSize.width() << "x" << targetSize.height()
	        << ", dpr=" << devicePixelRatioF()
	        << ", type=" << static_cast<int>(nativeRenderSourceType()) << logEnd;
	updateNativeWindowGeometry();
    updateOverlayWindowFilter();
#if CICI_OS_WINDOWS
    ::ShowWindow(reinterpret_cast<HWND>(nativeHandle_), SW_SHOW);
#endif
    QPointer<GPURenderWidget> self(this);
	const auto id = effectCtrl->registerNativeRenderTarget(
	    nativeHandle_, targetSize.width(), targetSize.height(),
								    nativeRenderSourceType(),
								    [self](int w, int h) {
        if (!self) {
		    return;
        }

        asyncRunUI([self, w, h]() {
		    if (!self) {
			    return;    
            }
            self->resizeNativeWindow(w, h);
	        });
        });
	if (id == 0) {
		destroyNativeWindow();
		return;
	}

	nativeRenderModel_ = model;
	nativeRenderId_ = id;
	LogInfo << "[KeyingPreview] registered widget=" << this
	        << ", controller=" << effectCtrl
	        << ", id=" << nativeRenderId_ << logEnd;
}

void GPURenderWidget::unregisterNativeWindow() {
	assert(isMainThread());
	FilterModel* model = nativeRenderModel_;
	const auto id = nativeRenderId_;
	nativeRenderModel_ = nullptr;
	nativeRenderId_ = 0;

	if (model != nullptr && id != 0) {
		auto* effectCtrl = model->effectController();
		LogInfo << "[KeyingPreview] unregister widget=" << this
		        << ", controller=" << effectCtrl
		        << ", id=" << id << logEnd;
		if (effectCtrl != nullptr) {
			effectCtrl->unregisterNativeRenderTarget(id);
		}
	}

	destroyNativeWindow();
}

void GPURenderWidget::resizeNativeWindow(int w, int h) {
	LogWarn << "not implemented now!!" << logEnd;
}
