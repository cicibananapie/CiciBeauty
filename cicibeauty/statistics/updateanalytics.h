// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <cstdint>
#include <string_view>

#include "update_common.h"

enum class UpdateCheckResult {
    NoUpdate,
    UpdateAvailable,
    Failed,
};

enum class UpdateUiAction {
    DialogShow,
    StartClick,
    RetryClick,
    DismissClick,
    CancelClick,
    RestartClick,
    CloseClick,
};

enum class UpdateResult {
    Succeeded,
    Failed,
    Cancelled,
    Interrupted,
    StartRequestFailed,
};

void reportUpdateCheckResult(UpdateCheckResult result,
                             UpdateType type,
                             std::u8string_view targetVersion);
void reportUpdateUiAction(UpdateUiAction action,
                          UpdateType type,
                          std::u8string_view targetVersion);
void reportUpdateStart(UpdateType type,
                       std::u8string_view targetVersion,
                       uint32_t attempt);
void reportUpdateResult(UpdateResult result,
                        UpdateType type,
                        std::u8string_view targetVersion,
                        uint32_t attempt);
