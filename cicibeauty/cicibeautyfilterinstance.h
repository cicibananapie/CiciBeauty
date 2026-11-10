// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "modelapp.h"
#include "obsinfo.h"

class CiciBeautyApp;
class EffectController;
class VideoClient;
typedef struct obs_source obs_source_t;
struct obs_source_frame;

class CiciBeautyFilterInstance : public FilterModel, private IObsEventCallback {
public:
    CiciBeautyFilterInstance(CiciBeautyApp* app, obs_source_t* filterSource);
    ~CiciBeautyFilterInstance();
    CiciBeautyFilterInstance(const CiciBeautyFilterInstance&) = delete;
    CiciBeautyFilterInstance& operator=(const CiciBeautyFilterInstance&) = delete;
    ObsInfo* obsInfo() const override;
    EffectController* effectController() const override;
    std::u8string sourceUuid() const override;
    bool effectParamValue(const char8_t* path, int& value) const override;
    bool defaultEffectParamValue(const char8_t* path, int& value) const override;
    bool canActivateEffect(uint64_t id, nlohmann::json& disablePatch) const override;
    void retryEffectPackageDownload(uint64_t id) override;
    void applySettingPatch(const nlohmann::json& patch) override;
    bool replaceSettingConfig(nlohmann::json& config) override;

private:
    friend class CiciBeautyApp;
    bool queryParam(std::string_view method, const char8_t* path, int& value) const;
    void onHVModeChanged(ObsInfo::HVMode mode) override; // IObsEventCallback
    void onCanvasTransformChanged(const ObsInfo::CanvasTransform& transform) override;

    obs_source_frame* onFilterVideo(obs_source_frame* frame);
    void onFilterRemove();
    void onFilterAdd(obs_source_t* source);

    void setPendingApplyTrigger(const std::u8string& trigger);
    void setPendingRemoveTrigger(const std::u8string& trigger);
    std::u8string consumePendingApplyTrigger(const std::u8string& defaultTrigger);
    std::u8string consumePendingRemoveTrigger(const std::u8string& defaultTrigger);
    CiciBeautyApp* app() const;
    const std::u8string& filterUuid() const;
    void detachObsBindings();

private:
    CiciBeautyApp* app_ = nullptr;

    const std::u8string filterUuid_;
    std::u8string parentUuid_;

    std::unique_ptr<ObsInfo> obsInfo_;
    std::unique_ptr<EffectController> effectCtrl_;
    std::unique_ptr<VideoClient> video_;
    std::atomic<uint32_t> canvasTransform_{0};

    std::atomic_bool cameraOpenReported_ = false;
    std::u8string pendingApplyTrigger_;
    std::u8string pendingRemoveTrigger_;
    std::mutex triggerMutex_;
    mutable std::mutex lifecycleMutex_;
};
