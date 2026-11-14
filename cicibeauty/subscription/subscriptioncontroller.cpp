// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "subscriptioncontroller.h"

#include "../lumaclient.h"
#include "log.h"

using namespace cici::protocol;

SubscriptionController::SubscriptionController() {
    LumaClient::instance().addListener(
        this,
        [this](const JsonMessage& message) {
            const auto& method = message.value["method"].get_ref<const std::string&>();
            const auto& body = message.value["body"];
            if (method != "subscription.updated" && method != "subscription.expired" &&
                method != "subscription.aboutToExpire") {
                return;
            }

            if (method == "subscription.expired" &&
                (!body.contains("previousType") || !body["previousType"].is_number_unsigned() ||
                 body["previousType"].get<uint64_t>() > Expired)) {
                return;
            }

            SubscriptionStatus status;
            if (!readSubscriptionStatus(body, status)) {
                LogWarn << "invalid subscription status in notification: " << method << logEnd;
                return;
            }
            requests_.reset();
            status_ = std::move(status);
            if (method == "subscription.updated") {
                emit subscriptionInfoUpdated();
            } else if (method == "subscription.expired") {
                emit expired(static_cast<SubscriptionType>(body["previousType"].get<unsigned>()));
            } else {
                emit aboutToExpire(status_.type, status_.daysBeforeExpire);
            }
        },
        [this]() {
            requests_.reset();
            status_ = SubscriptionStatus{};
            emit subscriptionInfoUpdated();
        });
}

SubscriptionController::~SubscriptionController() {
    LumaClient::instance().removeListener(this);
}

void SubscriptionController::refresh() {
    requests_.callAsync(u8"refresh", "subscription.refresh", nlohmann::json::object(), [this](CallResult result) {
        SubscriptionStatus status;
        if (result.ok() && readSubscriptionStatus(result.body, status)) {
            status_ = std::move(status);
            emit subscriptionInfoUpdated();
        } else {
            LogWarn << "subscription refresh failed, status=" << static_cast<int>(result.status) << logEnd;
        }
    });
}

void SubscriptionController::openPurchasePage() {
    if (!LumaClient::instance().postMsg(JsonMessage("subscription.openPurchase", nlohmann::json::object()))) {
        LogWarn << "send subscription purchase page request failed" << logEnd;
    }
}
