// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <algorithm>
#include <assert.h>
#include <cmath>
#include <obs.h>
#include <obs-frontend-api.h>
#include <thread>

#include <QPointer>

#include "obsinfo.h"
#include "cicibase.h"
#include "log.h"
#include "obscallbackthreadlogger.h"

namespace {

struct SceneItemFromSourceContext {
    obs_source_t* source = nullptr;
    obs_sceneitem_t* item = nullptr;
};

bool findSceneItemFromSource(obs_scene_t*, obs_sceneitem_t* item, void* param) {
    auto* ctx = static_cast<SceneItemFromSourceContext*>(param);
    if (ctx != nullptr && obs_sceneitem_get_source(item) == ctx->source) {
        ctx->item = item;
        obs_sceneitem_addref(item);
        return false;
    }

    return true;
}

obs_sceneitem_t* sceneItemFromSource(obs_scene_t* scene, obs_source_t* source) {
    SceneItemFromSourceContext ctx{source, nullptr};
    obs_scene_enum_items(scene, findSceneItemFromSource, &ctx);
    return ctx.item;
}

inline bool float_equal(float a, float b) {
	// FLT_EPSILON is the tiny value defined in float.h, roughly 1.19e-7.
	return fabsf(a - b) < 0.00001f;
}

inline float normalizeAngle(float angle) {
    while (angle >= 360.0f) {
        angle -= 360.0f;
    }
    while (angle < 0.0f) {
        angle += 360.0f;
    }
    return angle;
}

ObsInfo::RotationType toRotationType(float angle) {
    if (float_equal(angle, 0.0f)) {
        return ObsInfo::Rotate_0;
    }
    if (float_equal(angle, 90.0f)) {
        return ObsInfo::Rotate_90;
    }
    if (float_equal(angle, 180.0f)) {
        return ObsInfo::Rotate_180;
    }
    if (float_equal(angle, 270.0f)) {
        return ObsInfo::Rotate_270;
    }

    assert(false);
    return ObsInfo::Rotate_0;
}

}  // namespace

ObsInfo::~ObsInfo() {
    unbindSource();
}

void ObsInfo::bindSource(obs_source_t* source) {
    ObsUtility::VideoInputInfo input;
    std::optional<CanvasTransform> transform;
    {
        std::lock_guard<std::mutex> guard(bindingMutex_);
        unbindSourceLocked();
        if (source == nullptr) {
            return;
        }
        sourceUuid_ = to_u8(obs_source_get_uuid(source));
        setupVideoInputListener(source);
        input = ObsUtility::videoInputInfo(source);
        transform = setupTransformListener(source);
    }
    // Observer notifications may query state or rebind. Never deliver them under the binding lock.
    setVideoInputInfo(input);
    if (transform) {
        setCanvasTransform(*transform);
    }
}

void ObsInfo::unbindSource() {
    std::lock_guard<std::mutex> guard(bindingMutex_);
    unbindSourceLocked();
}

void ObsInfo::unbindSourceLocked() {
    disconnectTransformListener();
    disconnectVideoInputListener();
    sourceUuid_.clear();
}

void ObsInfo::refreshSceneBinding() {
    std::optional<CanvasTransform> transform;
    {
        std::lock_guard<std::mutex> guard(bindingMutex_);
        if (sourceUuid_.empty()) {
            disconnectTransformListener();
            return;
        }
        obs_source_t* source = obs_get_source_by_uuid(to_char(sourceUuid_));
        transform = setupTransformListener(source);
        obs_source_release(source);
    }
    if (transform) {
        setCanvasTransform(*transform);
    }
}

void ObsInfo::disconnectSceneBinding() {
    std::lock_guard<std::mutex> guard(bindingMutex_);
    disconnectTransformListener();
}

void ObsInfo::registerObserver(IObsEventCallback* cb) {
    if (auto it = std::find(callbacks_.begin(), callbacks_.end(), cb); it == callbacks_.end()) {
        callbacks_.push_back(cb);
    }
}

void ObsInfo::unregisterObserver(IObsEventCallback* cb) {
    if (auto it = std::find(callbacks_.begin(), callbacks_.end(), cb); it != callbacks_.end()) {
        callbacks_.erase(it);
    }
}

uint32_t ObsInfo::frameWidth() const {
    return w_;
}

uint32_t ObsInfo::frameHeight() const {
    return h_;
}

ObsInfo::HVMode ObsInfo::hvMode() const {
    return mode_;
}

const ObsInfo::CanvasTransform& ObsInfo::canvasTransform() const {
    return canvasTransform_;
}

const ObsUtility::VideoInputInfo& ObsInfo::videoInputInfo() const {
    return videoInputInfo_;
}

std::u8string ObsInfo::videoInputDisplayName() const {
    return videoInputInfo_.displayName_;
}

void ObsInfo::setVideoInputInfo(const ObsUtility::VideoInputInfo& info) {
    if (mainThreadId() != std::this_thread::get_id()) {
        const QPointer<ObsInfo> self(this);
        asyncRunUI([self, info]() {
            if (!self) {
                return;
            }
            self->onVideoInputChanged(info);
        });
    } else {
        onVideoInputChanged(info);
    }
}

void ObsInfo::onVideoInputChanged(const ObsUtility::VideoInputInfo& info) {
    if (videoInputInfo_.displayName_ == info.displayName_
        && videoInputInfo_.deviceName_ == info.deviceName_
        && videoInputInfo_.obsSourceName_ == info.obsSourceName_
        && videoInputInfo_.sourceId_ == info.sourceId_
        && videoInputInfo_.deviceId_ == info.deviceId_
        && videoInputInfo_.isVideoInput_ == info.isVideoInput_
        && videoInputInfo_.hasDeviceName_ == info.hasDeviceName_) {
        return;
    }

    videoInputInfo_ = info;
    for (auto observer: callbacks_) {
        observer->onVideoInputChanged(videoInputInfo_);
    }
}

void ObsInfo::setCanvasTransform(const CanvasTransform& transform) {
    if (mainThreadId() == std::this_thread::get_id()) {
        onCanvasTransformChanged(transform);
    } else {
        const QPointer<ObsInfo> self(this);
        asyncRunUI([self, transform]() {
            if (self) {
                self->onCanvasTransformChanged(transform);
            }
        });
    }
}

void ObsInfo::setFrameSize(uint32_t w, uint32_t h) {
    if (widthInWorkerThread_ != w || heightInWorkerThread_ != h) {
        widthInWorkerThread_ = w;
        heightInWorkerThread_ = h;
        
        if (mainThreadId() == std::this_thread::get_id()) {
            onFrameSizeChanged(w, h);
        }
        else {
            const QPointer<ObsInfo> self(this);
            asyncRunUI([self, w, h]() {
                if (!self) {
                    return;
                }
                self->onFrameSizeChanged(w, h);
            });
        }
    }
}

void ObsInfo::onCanvasTransformChanged(const CanvasTransform& transform) {
    const bool rotationChanged = canvasTransform_.rotation_ != transform.rotation_;
    const bool flipChanged = canvasTransform_.flipHorizontal_ != transform.flipHorizontal_
                          || canvasTransform_.flipVertical_ != transform.flipVertical_;

    canvasTransform_ = transform;
    if (rotationChanged || flipChanged) {
        for (auto observer: callbacks_) {
            observer->onCanvasTransformChanged(canvasTransform_);
        }
    }

    if (!rotationChanged) {
        return;
    }
    
    computeHVMode();
}

void ObsInfo::onFrameSizeChanged(uint32_t w, uint32_t h) {
    w_ = w;
    h_ = h;
    computeHVMode();
}

void ObsInfo::computeHVMode() {
    bool b1 = (w_ > h_);
    bool b2 = (canvasTransform_.rotation_ == Rotate_90 || canvasTransform_.rotation_ == Rotate_270);
    bool bVertical = (b1 && b2)||(!b1 && !b2);
    HVMode m = Unknown;
    if (bVertical) {
        m = VerticalMode;
    }
    else {
        m = HorizonalMode;
    }
    
    if (mode_ != m) {
        mode_ = m;
        for (auto observer: callbacks_) {
            observer->onHVModeChanged(m);
        }
    }
}

void ObsInfo::onTransform(void* data, calldata_t* cd) {
    logObsCallbackThread("ObsInfo::onTransform");
    auto* instance = reinterpret_cast<ObsInfo*>(data);
    if (instance != nullptr) {
        instance->handleTransform(cd);
    }
}

void ObsInfo::onVideoInputSourceUpdate(void* data, calldata_t* cd) {
    logObsCallbackThread("ObsInfo::onVideoInputSourceUpdate");
    auto* instance = reinterpret_cast<ObsInfo*>(data);
    if (instance != nullptr) {
        instance->handleVideoInputSourceUpdate(cd);
    }
}

std::optional<ObsInfo::CanvasTransform> ObsInfo::setupTransformListener(obs_source_t* source) {
    disconnectTransformListener();
    if (source == nullptr) {
        return std::nullopt;
    }

    obs_source_t* sceneSource = obs_frontend_get_current_scene();
    if (sceneSource == nullptr) {
        LogInfo << "ObsInfo::setupTransformListener deferred until a scene is active" << logEnd;
        return std::nullopt;
    }

    obs_scene_t* scene = obs_scene_from_source(sceneSource);
    if (scene == nullptr) {
        LogError << "ObsInfo::setupTransformListener: current source is not a scene" << logEnd;
        obs_source_release(sceneSource);
        return std::nullopt;
    }

    signal_handler_t* transformHandler = obs_source_get_signal_handler(sceneSource);
    if (transformHandler == nullptr) {
        LogError << "ObsInfo::setupTransformListener: signal handler is null" << logEnd;
        obs_source_release(sceneSource);
        return std::nullopt;
    }

    signal_handler_connect_ref(transformHandler, "item_transform", onTransform, this);
    transformHandler_ = transformHandler;

    std::optional<CanvasTransform> transform;
    obs_sceneitem_t* item = sceneItemFromSource(scene, source);
    if (item != nullptr) {
        obs_transform_info info = {};
        obs_sceneitem_get_info2(item, &info);
        transform = CanvasTransform{toRotationType(normalizeAngle(info.rot)), info.scale.x < 0.0f, info.scale.y < 0.0f};
        obs_sceneitem_release(item);
    }

    obs_source_release(sceneSource);
    return transform;
}

void ObsInfo::disconnectTransformListener() {
    if (transformHandler_ != nullptr) {
        signal_handler_disconnect(transformHandler_, "item_transform", onTransform, this);
        transformHandler_ = nullptr;
    }
}

void ObsInfo::handleTransform(calldata_t* cd) {
    if (cd == nullptr) {
        return;
    }

    obs_sceneitem_t* item = static_cast<obs_sceneitem_t*>(calldata_ptr(cd, "item"));
    obs_source_t* source = item != nullptr ? obs_sceneitem_get_source(item) : nullptr;
    if (source == nullptr) {
        return;
    }

    const std::u8string sourceUuid = to_u8(obs_source_get_uuid(source));
    // Binding changes disconnect this signal before mutating sourceUuid_.
    // Do not take bindingMutex_ here: disconnect waits for this callback.
    if (sourceUuid.empty() || sourceUuid != sourceUuid_) {
        return;
    }

    obs_transform_info info = {};
    obs_sceneitem_get_info2(item, &info);
    setCanvasTransform({toRotationType(normalizeAngle(info.rot)), info.scale.x < 0.0f, info.scale.y < 0.0f});
}

void ObsInfo::setupVideoInputListener(obs_source_t* source) {
    if (source == nullptr) {
        return;
    }

    signal_handler_t* videoInputHandler = obs_source_get_signal_handler(source);
    if (videoInputHandler == nullptr) {
        LogError << "ObsInfo::setupVideoInputListener: signal handler is null" << logEnd;
        return;
    }

    signal_handler_connect_ref(videoInputHandler, "update", onVideoInputSourceUpdate, this);
    videoInputHandler_ = videoInputHandler;
}

void ObsInfo::disconnectVideoInputListener() {
    if (videoInputHandler_ == nullptr) {
        return;
    }

    signal_handler_disconnect(videoInputHandler_, "update", onVideoInputSourceUpdate, this);
    videoInputHandler_ = nullptr;
}

void ObsInfo::handleVideoInputSourceUpdate(calldata_t* cd) {
    if (cd == nullptr) {
        return;
    }

    obs_source_t* source = static_cast<obs_source_t*>(calldata_ptr(cd, "source"));
    if (source == nullptr) {
        return;
    }

    const std::u8string sourceUuid = to_u8(obs_source_get_uuid(source));
    // Binding changes disconnect this signal before mutating sourceUuid_.
    // Do not take bindingMutex_ here: disconnect waits for this callback.
    if (sourceUuid.empty() || sourceUuid != sourceUuid_) {
        return;
    }

    setVideoInputInfo(ObsUtility::videoInputInfo(source));
}
