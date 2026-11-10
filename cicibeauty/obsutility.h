// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <string>

typedef struct obs_source obs_source_t;

namespace ObsUtility {

struct VideoInputInfo {
    std::u8string displayName_;
    std::u8string deviceName_;
    std::u8string obsSourceName_;
    std::u8string sourceId_;
    std::u8string deviceId_;
    bool isVideoInput_ = false;
    bool hasDeviceName_ = false;
};

VideoInputInfo videoInputInfo(obs_source_t* source);
std::u8string videoInputDisplayName(obs_source_t* source);
bool openVideoInputPropertyPage(const VideoInputInfo& info, void* parentWindow = nullptr);

}
