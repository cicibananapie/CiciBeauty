// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

struct ICiciBeautyPlugin {
    virtual bool onLoad() = 0;
    virtual void onUnLoad() = 0;
    virtual void onLocaleChanged(const char* locale) = 0;
};
