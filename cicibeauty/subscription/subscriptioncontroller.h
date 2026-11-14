// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QObject>

#include "protocol/account.h"
#include "../lumaclient.h"

// Qt interface with a local snapshot. ciciluma owns subscription checks and timers.
class SubscriptionController : public QObject {
    Q_OBJECT
public:
    SubscriptionController();
    ~SubscriptionController() override;

    void refresh();
    static void openPurchasePage();
    const std::u8string& uid() const {
        return status_.uid;
    }
    bool isVip() const {
        return status_.isVip;
    }
    SubscriptionType subscriptionType() const {
        return status_.type;
    }
    uint32_t howManyDaysBeforeExpire() const {
        return status_.daysBeforeExpire;
    }
    SubscriptionInfo subscriptionInfo() const {
        return status_.info;
    }

signals:
    void subscriptionInfoUpdated();
    void expired(SubscriptionType prevType);
    void aboutToExpire(SubscriptionType curType, uint32_t howManyDaysBeforeExpire);

private:
  LumaRequestScope requests_;
  SubscriptionStatus status_;
};
