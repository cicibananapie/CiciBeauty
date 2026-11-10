// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QString>

#include "../subscription/subscriptioncontroller.h"

namespace SubscriptionUi {

QString membershipTypeName(SubscriptionType type);
QString expirationDate(const SubscriptionInfo& info);

}
