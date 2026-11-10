// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QString>

#include "nlohmann/json_fwd.hpp"

namespace I18nSupport {

void setCurrentLocaleName(const QString& localeName);
QString currentLocaleName();
QString normalizeLocaleName(const QString& localeName);
QString localizedString(const nlohmann::json& i18nObject);
QString localizedName(const nlohmann::json& item);

}
