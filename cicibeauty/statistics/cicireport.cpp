// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "cicireport.h"

#include "log.h"
#include "protocol/statistics.h"
#include "../lumaclient.h"

void CiciReport::reportEvent(const std::u8string& event, const nlohmann::json& properties) {
    if (event.empty() || !properties.is_object()) {
        LogWarn << "ignored invalid statistics event" << logEnd;
        return;
    }
    LumaClient::instance().postMsg(cici::protocol::statisticsReport(event, properties));
}
