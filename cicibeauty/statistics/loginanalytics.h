// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

enum class LoginUiSurface {
    Modal,
    SettingsOverlay,
};

enum class LoginUiAction {
    Show,
    SignInClick,
    Close,
};

void reportLoginUiAction(LoginUiSurface surface, LoginUiAction action);
