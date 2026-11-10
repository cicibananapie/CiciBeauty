// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "subscriptionanalytics.h"

#include <cassert>

#include "eventid.h"
#include "nlohmann/json.hpp"
#include "cicireport.h"

namespace {

const char8_t* actionName(SubscriptionUiAction action) {
    switch (action) {
        case SubscriptionUiAction::PurchaseClick:
            return u8"purchase_click";
        case SubscriptionUiAction::PaymentCompletedClick:
            return u8"payment_completed_click";
        case SubscriptionUiAction::CheckRetryClick:
            return u8"check_retry_click";
        case SubscriptionUiAction::CheckBackClick:
            return u8"check_back_click";
        case SubscriptionUiAction::TipCloseClick:
            return u8"tip_close_click";
    }

    assert(false);
    return u8"unknown";
}

const char8_t* sourceName(SubscriptionUiSource source) {
    switch (source) {
        case SubscriptionUiSource::AccessOverlay:
            return u8"access_overlay";
        case SubscriptionUiSource::ExpirationTip:
            return u8"expiration_tip";
    }

    assert(false);
    return u8"unknown";
}

const char8_t* purchaseIntentName(SubscriptionPurchaseIntent purchaseIntent) {
    switch (purchaseIntent) {
        case SubscriptionPurchaseIntent::None:
            return nullptr;
        case SubscriptionPurchaseIntent::Subscribe:
            return u8"subscribe";
        case SubscriptionPurchaseIntent::Renew:
            return u8"renew";
    }

    assert(false);
    return nullptr;
}

} // namespace

void reportSubscriptionUiAction(SubscriptionUiAction action,
                                SubscriptionUiSource source,
                                SubscriptionPurchaseIntent purchaseIntent) {
    nlohmann::json properties = nlohmann::json::object({
        {"action", to_json(std::u8string(actionName(action)))},
        {"source", to_json(std::u8string(sourceName(source)))},
    });

    const char8_t* intentName = purchaseIntentName(purchaseIntent);
    if (intentName != nullptr) {
        properties["purchase_intent"] = to_json(std::u8string(intentName));
    }

    CiciReport::reportEvent(KEVT_APP_SUBSCRIPTION_UI_ACTION, properties);
}
