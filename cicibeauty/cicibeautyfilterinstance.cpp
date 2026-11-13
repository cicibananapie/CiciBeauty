// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "cicibeautyfilterinstance.h"

#include <chrono>
#include <obs.h>
#include <string>
#include <string_view>
#include <vector>

#include "cicibeautyapp.h"
#include "lumaclient.h"
#include "videoclient.h"
#include "protocol/settings.h"
#include "log.h"
#include "nlohmann/json.hpp"
#include "obsinfo.h"
#include "settingconfig.h"
#include "statistics/eventid.h"
#include "statistics/cicireport.h"

namespace {

std::u8string obsSourceUuid(obs_source_t* source) {
    const char8_t* uuid = source != nullptr ? to_u8(obs_source_get_uuid(source)) : nullptr;
    return uuid != nullptr ? std::u8string(uuid) : std::u8string();
}

std::u8string obsSourceType(obs_source_t* source) {
    const char8_t* type = source != nullptr ? to_u8(obs_source_get_id(source)) : nullptr;
    return type != nullptr ? std::u8string(type) : std::u8string();
}

std::u8string obsSourceTypeByUuid(std::u8string_view sourceUuid) {
    if (sourceUuid.empty()) {
        return {};
    }

    const std::u8string uuid(sourceUuid);
    obs_source_t* source = obs_get_source_by_uuid(to_char(uuid));
    if (source == nullptr) {
        return {};
    }

    const std::u8string sourceType = obsSourceType(source);
    obs_source_release(source);
    return sourceType;
}

} // namespace

CiciBeautyFilterInstance::CiciBeautyFilterInstance(CiciBeautyApp* app, obs_source_t* filterSource)
    : app_(app), filterUuid_(obsSourceUuid(filterSource)) {

    obsInfo_ = std::make_unique<ObsInfo>();
    obsInfo_->registerObserver(this);
    video_ = std::make_unique<VideoClient>();

    LogInfo << "CiciBeautyFilterInstance created filter_uuid=" << filterUuid_ << logEnd;
}

CiciBeautyFilterInstance::~CiciBeautyFilterInstance() {
    detachObsBindings();
    obsInfo_->unregisterObserver(this);
    LogInfo << "CiciBeautyFilterInstance destroyed filter_uuid=" << filterUuid_ << logEnd;
}

obs_source_frame* CiciBeautyFilterInstance::onFilterVideo(obs_source_frame* frame) {
    obsInfo_->setFrameSize(frame->width, frame->height);
    if (!cameraOpenReported_.exchange(true)) {
        const std::u8string sourceUuid = this->sourceUuid();
        CiciReport::reportEvent(KEVT_APP_CAMERA_OPEN_SUCCESS,
                                nlohmann::json::object({
                                    {"filter_uuid", to_char(filterUuid_)},
                                    {"source_uuid", to_char(sourceUuid)},
                                    {"source_type", to_char(obsSourceTypeByUuid(sourceUuid))},
                                    {"width", frame->width},
                                    {"height", frame->height},
                                }));
    }
    video_->process(*frame, canvasTransform_.load(std::memory_order_relaxed));

    return frame;
}

void CiciBeautyFilterInstance::onFilterRemove() {
    const std::u8string parentUuid = sourceUuid();
    LogInfo << "CiciBeautyFilterInstance::onFilterRemove source_uuid=" << parentUuid << " filter_uuid=" << filterUuid_
            << logEnd;
    detachObsBindings();
}

void CiciBeautyFilterInstance::onFilterAdd(obs_source_t* source) {
    const std::u8string parentUuid = obsSourceUuid(source);
    LogInfo << "CiciBeautyFilterInstance::onFilterAdd source_uuid=" << parentUuid << " filter_uuid=" << filterUuid_
            << logEnd;
    {
        std::lock_guard<std::mutex> guard(lifecycleMutex_);
        parentUuid_ = parentUuid;
        obsInfo_->bindSource(source);
    }
    const auto videoChannel = video_->start();
    LumaClient::instance().postMsg(cici::protocol::instanceMessage("instance.create", filterUuid_,
                                                                   {{"sourceUuid", to_json(parentUuid)},
                                                                    {"sourceType", to_json(obsSourceType(source))},
                                                                    {"videoChannel", to_json(videoChannel)}}));
    onHVModeChanged(obsInfo_->hvMode());
    onCanvasTransformChanged(obsInfo_->canvasTransform());
}

void CiciBeautyFilterInstance::setPendingApplyTrigger(const std::u8string& trigger) {
    std::lock_guard<std::mutex> guard(triggerMutex_);
    pendingApplyTrigger_ = trigger;
}

void CiciBeautyFilterInstance::setPendingRemoveTrigger(const std::u8string& trigger) {
    std::lock_guard<std::mutex> guard(triggerMutex_);
    pendingRemoveTrigger_ = trigger;
}

CiciBeautyApp* CiciBeautyFilterInstance::app() const {
    return app_;
}

const std::u8string& CiciBeautyFilterInstance::filterUuid() const {
    return filterUuid_;
}

ObsInfo* CiciBeautyFilterInstance::obsInfo() const {
    return obsInfo_.get();
}

std::u8string CiciBeautyFilterInstance::sourceUuid() const {
    std::lock_guard<std::mutex> guard(lifecycleMutex_);
    return parentUuid_;
}

void CiciBeautyFilterInstance::detachObsBindings() {
    std::lock_guard<std::mutex> guard(lifecycleMutex_);
    video_->stop();
    if (!parentUuid_.empty()) {
        LumaClient::instance().postMsg(cici::protocol::instanceMessage("instance.destroy", filterUuid_));
    }
    obsInfo_->unbindSource();
    parentUuid_.clear();
}

void CiciBeautyFilterInstance::onCanvasTransformChanged(const ObsInfo::CanvasTransform& transform) {
    uint32_t rotation = transform.rotation_ == ObsInfo::Rotate_90    ? 90
                        : transform.rotation_ == ObsInfo::Rotate_180 ? 180
                        : transform.rotation_ == ObsInfo::Rotate_270 ? 270
                                                                     : 0;
    canvasTransform_.store(rotation | (uint32_t(transform.flipHorizontal_) << 16) |
                               (uint32_t(transform.flipVertical_) << 17),
                           std::memory_order_relaxed);
}

std::u8string CiciBeautyFilterInstance::consumePendingApplyTrigger(const std::u8string& defaultTrigger) {
    std::lock_guard<std::mutex> guard(triggerMutex_);
    std::u8string trigger = std::move(pendingApplyTrigger_);
    pendingApplyTrigger_.clear();
    return trigger.empty() ? defaultTrigger : trigger;
}

std::u8string CiciBeautyFilterInstance::consumePendingRemoveTrigger(const std::u8string& defaultTrigger) {
    std::lock_guard<std::mutex> guard(triggerMutex_);
    std::u8string trigger = std::move(pendingRemoveTrigger_);
    pendingRemoveTrigger_.clear();
    return trigger.empty() ? defaultTrigger : trigger;
}

bool CiciBeautyFilterInstance::effectParamValue(const char8_t* path, int& value) const {
    return queryParam("instance.getParamValue", path, value);
}
bool CiciBeautyFilterInstance::defaultEffectParamValue(const char8_t* path, int& value) const {
    return queryParam("instance.getDefaultParamValue", path, value);
}
bool CiciBeautyFilterInstance::queryParam(std::string_view method, const char8_t* path, int& value) const {
    if (!path || sourceUuid().empty()) {
        return false;
    }
    const auto result = LumaClient::instance().call(
        method, {{"instanceId", to_json(filterUuid_)}, {"path", to_char(path)}}, std::chrono::milliseconds(100));
    if (result.status != cici::protocol::CallStatus::Success) {
        LogWarn << "query effect parameter failed, method=" << method << ", status=" << static_cast<int>(result.status)
                << logEnd;
        return false;
    }
    const auto& body = result.body;
    if (!body.contains("found") || !body["found"].is_boolean() || !body["found"].get<bool>() ||
        !body.contains("value") || !body["value"].is_number_integer()) {
        return false;
    }
    value = body["value"].get<int>();
    return true;
}
void CiciBeautyFilterInstance::canActivateEffect(
    QObject* owner, uint64_t id, std::function<void(bool, const nlohmann::json&)> callback) const {
    LumaClient::instance().callAsync(
        owner, "instance.canActivateEffect", {{"instanceId", to_json(filterUuid_)}, {"effectId", id}},
        [callback = std::move(callback)](cici::protocol::CallResult result) {
            const auto& body = result.body;
            if (!result.ok() || !body.contains("allowed") || !body["allowed"].is_boolean() ||
                !body.contains("disablePatch") || !body["disablePatch"].is_array() ||
                body["allowed"].get<bool>() != body["disablePatch"].empty()) {
                LogWarn << "query effect conflict failed, status=" << static_cast<int>(result.status) << logEnd;
                callback(false, nlohmann::json::array());
                return;
            }
            callback(body["allowed"].get<bool>(), body["disablePatch"]);
        });
}
void CiciBeautyFilterInstance::retryEffectPackageDownload(uint64_t id) {
    LumaClient::instance().postMsg(
        cici::protocol::instanceMessage("instance.retryPackage", filterUuid_, {{"effectId", id}}));
}
void CiciBeautyFilterInstance::applySettingPatch(const nlohmann::json& patch) {
    auto* settings = app_->settingConfig();
    if (sourceUuid().empty()) {
        return;
    }
    settings->applyPatch(filterUuid_, patch);
}
bool CiciBeautyFilterInstance::replaceSettingConfig(nlohmann::json& config) {
    return !sourceUuid().empty() && app_->settingConfig()->setConfig(filterUuid_, config);
}
void CiciBeautyFilterInstance::onHVModeChanged(ObsInfo::HVMode mode) {
    LumaClient::instance().postMsg(
        cici::protocol::instanceMessage("instance.setMode", filterUuid_, {{"mode", static_cast<uint32_t>(mode)}}));
}
