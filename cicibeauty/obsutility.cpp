// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "obsutility.h"

#if CICI_OS_WINDOWS
#include <string_view>
#endif

#include <obs.h>
#include "log.h"

namespace {
std::u8string obsString(const char* str) {
    if (str == nullptr || *str == '\0') {
        return {};
    }

    return to_u8(str);
}

#if CICI_OS_WINDOWS
void replaceAll(std::u8string& str, std::u8string_view from, std::u8string_view to) {
    if (from.empty()) {
        return;
    }

    size_t pos = 0;
    while ((pos = str.find(from.data(), pos, from.size())) != std::u8string::npos) {
        str.replace(pos, from.size(), to.data(), to.size());
        pos += to.size();
    }
}
#endif  // CICI_OS_WINDOWS

std::u8string obsSourceName(obs_source_t* source) {
    return obsString(obs_source_get_name(source));
}

std::u8string obsSourceId(obs_source_t* source) {
    const char* id = obs_source_get_unversioned_id(source);
    if (id == nullptr || *id == '\0') {
        id = obs_source_get_id(source);
    }

    return obsString(id);
}

std::u8string obsDataString(obs_data_t* data, const char* key) {
    if (data == nullptr || key == nullptr) {
        return {};
    }

    return obsString(obs_data_get_string(data, key));
}

#if CICI_OS_WINDOWS
std::u8string decodeDshowString(std::u8string value) {
    replaceAll(value, u8"#3A", u8":");
    replaceAll(value, u8"#22", u8"#");
    return value;
}

std::u8string dshowDeviceNameFromId(const std::u8string& deviceId) {
    if (deviceId.empty()) {
        return {};
    }

    const size_t separator = deviceId.find(u8":");
    std::u8string encodedName = separator == std::u8string::npos
        ? deviceId
        : deviceId.substr(0, separator);

    return decodeDshowString(encodedName);
}

bool openObsDshowSourceDialog(const ObsUtility::VideoInputInfo& info) {
    if (info.obsSourceName_.empty() || info.sourceId_ != u8"dshow_input") {
        return false;
    }

    obs_source_t* source = obs_get_source_by_name(to_char(info.obsSourceName_));
    if (source == nullptr) {
        LogWarn << "openObsDshowSourceDialog: failed to find OBS source by name" << logEnd;
        return false;
    }

    bool opened = false;
    do {
        if (obsSourceId(source) != info.sourceId_) {
            LogWarn << "openObsDshowSourceDialog: source id mismatch" << logEnd;
            break;
        }

        obs_properties_t* props = obs_source_properties(source);
        if (props == nullptr) {
            LogWarn << "openObsDshowSourceDialog: obs_source_properties returned null" << logEnd;
            break;
        }

        obs_property_t* button = obs_properties_get(props, "video_config");
        if (button == nullptr) {
            LogWarn << "openObsDshowSourceDialog: cannot find video_config button" << logEnd;
            obs_properties_destroy(props);
            break;
        }

        obs_property_button_clicked(button, source);
        obs_properties_destroy(props);
        opened = true;
    } while (false);

    obs_source_release(source);
    return opened;
}

void fillDshowVideoInputInfo(obs_source_t* source, ObsUtility::VideoInputInfo& info) {
    info.isVideoInput_ = true;

    obs_data_t* settings = obs_source_get_settings(source);
    if (settings == nullptr) {
        return;
    }

    info.deviceId_ = obsDataString(settings, "video_device_id");
    obs_data_release(settings);

    info.deviceName_ = dshowDeviceNameFromId(info.deviceId_);
    info.hasDeviceName_ = !info.deviceName_.empty();
    if (info.hasDeviceName_) {
        info.displayName_ = info.deviceName_;
    }
}
#endif  // CICI_OS_WINDOWS

#if CICI_OS_MACOS
bool isMacAvCaptureSource(const std::u8string& sourceId) {
    return sourceId == u8"macos-avcapture"
        || sourceId == u8"macos-avcapture-fast"
        || sourceId == u8"av_capture_input";
}

void fillMacAvCaptureVideoInputInfo(obs_source_t* source, ObsUtility::VideoInputInfo& info) {
    info.isVideoInput_ = true;

    obs_data_t* settings = obs_source_get_settings(source);
    if (settings == nullptr) {
        return;
    }

    info.deviceId_ = obsDataString(settings, "device");
    info.deviceName_ = obsDataString(settings, "device_name");
    obs_data_release(settings);

    info.hasDeviceName_ = !info.deviceName_.empty();
    if (info.hasDeviceName_) {
        info.displayName_ = info.deviceName_;
    }
}
#endif  // CICI_OS_MACOS
}

namespace ObsUtility {

VideoInputInfo videoInputInfo(obs_source_t* source) {
    VideoInputInfo info;
    if (source == nullptr) {
        return info;
    }

    info.obsSourceName_ = obsSourceName(source);
    info.sourceId_ = obsSourceId(source);

#if CICI_OS_WINDOWS
    if (info.sourceId_ == u8"dshow_input") {
        fillDshowVideoInputInfo(source, info);
    }
#elif CICI_OS_MACOS
    if (isMacAvCaptureSource(info.sourceId_)) {
        fillMacAvCaptureVideoInputInfo(source, info);
    }
#endif

    if (info.displayName_.empty()) {
        info.displayName_ = info.obsSourceName_;
    }

    return info;
}

std::u8string videoInputDisplayName(obs_source_t* source) {
    return videoInputInfo(source).displayName_;
}

bool openVideoInputPropertyPage(const VideoInputInfo& info, void* parentWindow) {
#if CICI_OS_WINDOWS
    UNUSED_PARAMETER(parentWindow);
    return openObsDshowSourceDialog(info);
#else
    UNUSED_PARAMETER(info);
    UNUSED_PARAMETER(parentWindow);
    return false;
#endif
}

}
