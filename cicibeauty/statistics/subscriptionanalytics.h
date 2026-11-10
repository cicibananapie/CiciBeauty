// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

enum class SubscriptionUiAction {
    PurchaseClick,
    PaymentCompletedClick,
    CheckRetryClick,
    CheckBackClick,
    TipCloseClick,
};

enum class SubscriptionUiSource {
    AccessOverlay,
    ExpirationTip,
};

enum class SubscriptionPurchaseIntent {
    None,
    Subscribe,
    Renew,
};

void reportSubscriptionUiAction(
    SubscriptionUiAction action,
    SubscriptionUiSource source,
    SubscriptionPurchaseIntent purchaseIntent = SubscriptionPurchaseIntent::None);
