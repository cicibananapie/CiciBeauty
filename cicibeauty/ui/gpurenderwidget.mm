// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "gpurenderwidget.h"
#if CICI_OS_MACOS
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <IOSurface/IOSurface.h>
#elif CICI_OS_WINDOWS
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
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
#include <array>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

#include "log.h"
#include "cicibase.h"
#include "../modelapp.h"
#include "../obsinfo.h"
#include "../lumaclient.h"
#include "protocol/message.h"
#include "shmframe.h"
#include "uuid.h"

using namespace cici::protocol;

struct GPURenderWidget::Preview {
    SHMFrame queue;
    std::thread consumer;
    std::mutex mutex;
    std::condition_variable ready;
    enum class Submission {
        Waiting,
        Skipped,
        Submitted
    };
    Submission submission = Submission::Waiting;
    bool stopping = false;
    QSize renderSize;
    LumaRequestScope colorPickRequests;
#if CICI_OS_MACOS
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commands = nil;
    id<MTLTexture> textures[kPreviewSlots]{};
    id<MTLCommandBuffer> command = nil;
    CAMetalLayer* layer = nil;
#else
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> textures[kPreviewSlots];
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> mutexes[kPreviewSlots];
    Microsoft::WRL::ComPtr<ID3D11Query> completed;
#endif

    ~Preview() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            queue.close();
        }
        ready.notify_one();
        if (consumer.joinable()) {
            consumer.join();
        }
#if CICI_OS_MACOS
        [command release];
        [layer removeFromSuperlayer];
        [layer release];
        for (auto texture : textures) {
            [texture release];
        }
        [commands release];
        [device release];
#endif
    }

    bool open(void* window, const QSize& size, const std::array<uint32_t, kPreviewSlots>& handles, uint64_t adapterId) {
#if CICI_OS_MACOS
        device = MTLCreateSystemDefaultDevice();
        commands = [device newCommandQueue];
        auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                              width:size.width() height:size.height() mipmapped:NO];
        descriptor.usage = MTLTextureUsageShaderRead;
        descriptor.storageMode = MTLStorageModeShared;
        for (uint32_t i = 0; i < kPreviewSlots; ++i) {
            auto surface = IOSurfaceLookup(handles[i]);
            if (!surface) {
                return false;
            }
            textures[i] = [device newTextureWithDescriptor:descriptor iosurface:surface plane:0];
            CFRelease(surface);
            if (!textures[i]) {
                return false;
            }
        }
        NSView* view = (__bridge NSView*)window;
        view.wantsLayer = YES;
        layer = [[CAMetalLayer alloc] init];
        layer.device = device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = NO;
        layer.displaySyncEnabled = NO;
        layer.frame = view.bounds;
        layer.drawableSize = CGSizeMake(size.width(), size.height());
        [view.layer addSublayer:layer];
        return device && commands;
#else
        using Microsoft::WRL::ComPtr;
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            return false;
        }
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            uint64_t luid = 0;
            std::memcpy(&luid, &desc.AdapterLuid, sizeof(luid));
            if (luid == adapterId) {
                break;
            }
            adapter.Reset();
        }
        if (!adapter || FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) {
            return false;
        }
        for (uint32_t i = 0; i < kPreviewSlots; ++i) {
            if (FAILED(device->OpenSharedResource(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(handles[i])),
                                                  IID_PPV_ARGS(&textures[i]))) ||
                FAILED(textures[i].As(&mutexes[i]))) {
                return false;
            }
            D3D11_TEXTURE2D_DESC desc{};
            textures[i]->GetDesc(&desc);
            if (desc.Width != static_cast<UINT>(size.width()) || desc.Height != static_cast<UINT>(size.height()) ||
                desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
                return false;
            }
        }
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = size.width();
        desc.BufferDesc.Height = size.height();
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.OutputWindow = static_cast<HWND>(window);
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
        return SUCCEEDED(factory->CreateSwapChain(device.Get(), &desc, &swap)) &&
               SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) &&
               SUCCEEDED(device->CreateQuery(&query, &completed));
#endif
    }

    void start(std::function<void(PreviewFrame)> consume) {
        consumer = std::thread([this, consume = std::move(consume)]() {
            while (queue.acquireRead(true) == SHMFrame::Result::Success) {
                // Skip queued older pictures, with the same bounded catch-up as before.
                for (uint32_t n = 1; n < kPreviewSlots && queue.readableCount() > 1; ++n) {
                    queue.releaseRead();
                    if (queue.acquireRead() != SHMFrame::Result::Success) {
                        break;
                    }
                }
                if (queue.readView().size() != sizeof(PreviewFrame)) {
                    break;
                }
                PreviewFrame frame;
                std::memcpy(&frame, queue.readView().data(), sizeof(frame));
                if (frame.slot >= kPreviewSlots || frame.renderWidth == 0 || frame.renderHeight == 0) {
                    break;
                }
                consume(frame);
                std::unique_lock lock(mutex);
                ready.wait(lock, [this]() {
                    return submission != Submission::Waiting || stopping;
                });
                const bool copied = submission == Submission::Submitted;
                submission = Submission::Waiting;
                lock.unlock();
                if (copied) {
#if CICI_OS_MACOS
                    [command waitUntilCompleted];
#else
                    // UI submits one copy at a time; it cannot use this context
                    // again until this thread finishes the query and releases the read.
                    while (context->GetData(completed.Get(), nullptr, 0, 0) == S_FALSE) {
                        Sleep(1);
                    }
                    mutexes[frame.slot]->ReleaseSync(0);
#endif
                }
                queue.releaseRead();
            }
            if (!queue.readView().empty()) {
                queue.releaseRead();
            }
            consume({}); // A closed/invalid queue ends this preview on the UI thread.
        });
    }

    void submit(uint32_t slot) {
        const bool copied = present(slot);
        {
            std::lock_guard lock(mutex);
            submission = copied ? Submission::Submitted : Submission::Skipped;
        }
        ready.notify_one();
    }

    bool present(uint32_t slot) {
#if CICI_OS_MACOS
        auto drawable = [layer nextDrawable];
        if (!drawable) {
            return false;
        }
        [command release];
        command = [[commands commandBuffer] retain];
        auto blit = [command blitCommandEncoder];
        [blit copyFromTexture:textures[slot] sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(textures[slot].width, textures[slot].height, 1)
                    toTexture:drawable.texture destinationSlice:0 destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];
        [command presentDrawable:drawable];
        [command commit];
#else
        if (mutexes[slot]->AcquireSync(0, 0) != S_OK) {
            return false;
        }
        context->CopyResource(backBuffer.Get(), textures[slot].Get());
        context->End(completed.Get());
        context->Flush();
        swap->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
#endif
        return true;
    }
};

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
    LumaClient::instance().removeListener(this);
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
    if (!previewId_.empty()) {
        unregisterNativeWindow();
        registerNativeWindow();
    }
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

GPURenderWidget::NativeRenderSourceType GPURenderWidget::nativeRenderSourceType() const {
    return NativeRenderSourceType::Output;
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

    if (isVisible() && newModel != nullptr && previewId_.empty()) {
        registerNativeWindow();
    }
}

void GPURenderWidget::registerNativeWindow() {
	assert(isMainThread());
	if (!previewId_.empty()) {
		return;
	}

	auto* model = app_->filterModel();
	if (model == nullptr) {
		return;
	}

	int w = 0;
	int h = 0;
    auto* info = model->obsInfo();
    w = static_cast<int>(info->frameWidth());
    h = static_cast<int>(info->frameHeight());
    const auto rotation = info->canvasTransform().rotation_;
    if (rotation == ObsInfo::Rotate_90 || rotation == ObsInfo::Rotate_270) {
        std::swap(w, h);
    }
	const QSize previewSize = scaledPreviewSizeFromRenderSize(w, h);
	if (previewSize.isEmpty()) {
		LogWarn << "[KeyingPreview] registration skipped widget=" << this
		        << ", renderSize=" << w << "x" << h << logEnd;
		return;
	}

	setFixedSize(previewSize);
	createNativeWindow();
	const QSize targetSize = nativePixelSize();
	LogInfo << "[KeyingPreview] register widget=" << this
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
    nativeRenderModel_ = model;
    instanceId_ = model->filterUuid();
    previewId_ = CiciUUID::generate();
    preview_ = std::make_unique<Preview>();
    preview_->renderSize = QSize(w, h);
    LumaClient::instance().addListener(this, [this](const JsonMessage& message) {
        onPreviewMessage(message);
    }, [this]() {
        unregisterNativeWindow();
    });
    const auto channel = u8"p" + previewId_.substr(0, 22);
    if (!LumaClient::instance().postMsg(JsonMessage("preview.open", {
            {"instanceId", to_char(instanceId_)}, {"previewId", to_char(previewId_)},
            {"channel", to_char(channel)}, {"source", static_cast<uint32_t>(nativeRenderSourceType())},
            {"width", targetSize.width()}, {"height", targetSize.height()}}))) {
        unregisterNativeWindow();
    }
}

void GPURenderWidget::unregisterNativeWindow() {
	assert(isMainThread());
    const auto id = std::exchange(previewId_, {});
    const auto instance = std::exchange(instanceId_, {});
    nativeRenderModel_ = nullptr;
    preview_.reset(); // Finish GPU reads before returning their queue leases.
    LumaClient::instance().removeListener(this);
    if (!id.empty()) {
        LumaClient::instance().postMsg(JsonMessage("preview.close", {
            {"instanceId", to_char(instance)}, {"previewId", to_char(id)}}));
    }
    destroyNativeWindow();
}

void GPURenderWidget::resizeNativeWindow(int w, int h) {
    const auto size = scaledPreviewSizeFromRenderSize(w, h);
    if (!size.isEmpty() && size != this->size()) {
        setFixedSize(size);
    }
}

void GPURenderWidget::onPreviewMessage(const JsonMessage& message) {
    const auto& body = message.value["body"];
    if (previewId_.empty() || body.value("previewId", std::string()) != to_char(previewId_)) {
        return;
    }
    const auto method = message.value["method"].get<std::string>();
    if (method == "preview.ready" && !preview_->consumer.joinable()) {
        if (!body.contains("handles") || !body["handles"].is_array() || body["handles"].size() != kPreviewSlots) {
            return;
        }
        const auto size = QSize(body.value("width", 0), body.value("height", 0));
        if (size != nativePixelSize() || !preview_->queue.open(to_u8(body.value("channel", std::string()))) ||
            preview_->queue.capacity() != kPreviewSlots || preview_->queue.frameBytes() != sizeof(PreviewFrame) ||
            !preview_->open(nativeHandle_, size, body["handles"].get<std::array<uint32_t, kPreviewSlots>>(),
                            body.value("adapter", uint64_t{0}))) {
            LogError << "Open shared preview failed: " << previewId_ << logEnd;
            unregisterNativeWindow();
            return;
        }
        QPointer<GPURenderWidget> self(this);
        const auto id = previewId_;
        preview_->start([self, id](PreviewFrame frame) {
            if (self) {
                QMetaObject::invokeMethod(self, [self, id, frame]() {
                    if (self && self->previewId_ == id) {
                        self->consumePreview(frame);
                    }
                }, Qt::QueuedConnection);
            }
        });
    } else if (method == "preview.failed") {
        LogWarn << "Preview request failed: " << previewId_ << logEnd;
        unregisterNativeWindow();
    }
}

void GPURenderWidget::consumePreview(const PreviewFrame& frame) {
    if (frame.slot >= kPreviewSlots || frame.renderWidth == 0 || frame.renderHeight == 0) {
        unregisterNativeWindow();
        return;
    }
    auto& p = *preview_;
    p.submit(frame.slot);
    const QSize renderSize(frame.renderWidth, frame.renderHeight);
    if (p.renderSize != renderSize) {
        p.renderSize = renderSize;
        const auto id = previewId_;
        QPointer<GPURenderWidget> self(this);
        QMetaObject::invokeMethod(this, [self, id, renderSize]() {
            if (self && self->previewId_ == id) {
                self->resizeNativeWindow(renderSize.width(), renderSize.height());
            }
        }, Qt::QueuedConnection);
    }
}

void GPURenderWidget::updateColorPickMagnifier(int x, int y, float radius, float zoomRatio, bool active) {
    if (!previewId_.empty()) {
        LumaClient::instance().postMsg(JsonMessage("preview.magnifier", {
            {"instanceId", to_char(instanceId_)}, {"previewId", to_char(previewId_)},
            {"x", std::clamp(x, 0, nativePixelSize().width() - 1)},
            {"y", std::clamp(y, 0, nativePixelSize().height() - 1)},
            {"radius", radius}, {"zoom", zoomRatio}, {"active", active}}));
    }
}

void GPURenderWidget::requestColorPick(int x, int y, std::function<void(uint32_t)> callback) {
    if (previewId_.empty()) {
        return;
    }
    preview_->colorPickRequests.callAsync(
        u8"color", "preview.pickColor",
        {{"instanceId", to_char(instanceId_)}, {"previewId", to_char(previewId_)}, {"x", x}, {"y", y}},
        [this, callback = std::move(callback)](CallResult result) {
            if (!result.ok() || !result.body.contains("rgba") || !result.body["rgba"].is_number_unsigned()) {
                LogWarn << "Color pick request failed, status=" << static_cast<int>(result.status) << logEnd;
                unregisterNativeWindow();
                return;
            }
            if (callback) {
                callback(result.body["rgba"].get<uint32_t>());
            }
        });
}

void GPURenderWidget::autoKeying(GlobalModel* app, QObject* owner, std::function<void(uint32_t, float)> callback) {
    auto* model = app->filterModel();
    if (!model) {
        return;
    }
    const auto instance = model->filterUuid();
    LumaClient::instance().callAsync(owner, "preview.autoKeying", {{"instanceId", to_char(instance)}},
        [instance, app, callback = std::move(callback)](CallResult result) {
            const auto& body = result.body;
            if (!result.ok() || !body.contains("rgba") || !body["rgba"].is_number_unsigned() ||
                !body.contains("similarity") || !body["similarity"].is_number()) {
                LogWarn << "Auto keying request failed, status=" << static_cast<int>(result.status) << logEnd;
                return;
            }
            if (app->filterModel() && app->filterModel()->filterUuid() == instance) {
                callback(body["rgba"].get<uint32_t>(), body["similarity"].get<float>());
            }
        });
}
