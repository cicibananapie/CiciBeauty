// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "subscriptionuiformatter.h"

#include <chrono>

#include <QDateTime>
#include <QObject>

namespace SubscriptionUi {

QString membershipTypeName(SubscriptionType type) {
    switch (type) {
    case Trial:
        return QObject::tr("Trial");
    case Monthly:
    case OneMonth:
        return QObject::tr("Monthly");
    case Yearly:
    case OneYear:
        return QObject::tr("Yearly");
    default:
        return QObject::tr("VIP");
    }
}

QString expirationDate(const SubscriptionInfo& info) {
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        info.expireTime_.time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(milliseconds)
        .toLocalTime()
        .date()
        .toString(Qt::ISODate);
}

}
