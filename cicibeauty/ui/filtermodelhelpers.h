// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <assert.h>
#include <stdint.h>

#include "../obsinfo.h"
#include "../modelapp.h"

inline ObsInfo* currentObsInfo(GlobalModel* app) {
    assert(app != nullptr);
    auto* model = app->filterModel();
    return model != nullptr ? model->obsInfo() : nullptr;
}

inline bool compatibleAtHVMode(uint32_t hv, ObsInfo::HVMode mode) {
    switch (mode) {
        case ObsInfo::HorizonalMode:
            return hv != 2;
        case ObsInfo::VerticalMode:
            return hv != 1;
        case ObsInfo::Unknown:
            return hv == 0 || hv == 3;
        default:
            assert(false);
            return false;
    }
}
