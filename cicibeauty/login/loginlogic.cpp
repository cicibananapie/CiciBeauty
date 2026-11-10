// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "loginlogic.h"

#include "../lumaclient.h"
#include "log.h"

using namespace cici::protocol;

namespace {
bool post(std::string_view method) {
    if (LumaClient::instance().postMsg(JsonMessage(method, nlohmann::json::object()))) {
        return true;
    }
    LogWarn << "send login operation failed: " << method << logEnd;
    return false;
}
}

LoginLogic::LoginLogic(QObject* parent) : QObject(parent) {
    LumaClient::instance().addListener(
        this,
        [this](const JsonMessage& message) {
            const auto& method = message.value["method"].get_ref<const std::string&>();
            const auto& body = message.value["body"];
            if (method == "login.interactiveReady") {
                emit interactiveLoginReady();
                return;
            }
            if (method != "login.ready" && method != "login.loggedOut" && method != "login.failed") {
                return;
            }

            if (method == "login.ready" &&
                (!body.contains("source") || !body["source"].is_number_unsigned() ||
                 body["source"].get<uint64_t>() > static_cast<unsigned>(LoginReadySource::Server))) {
                return;
            }
            if (method == "login.failed" &&
                (!body.contains("reason") || !body["reason"].is_number_unsigned() ||
                 body["reason"].get<uint64_t>() > static_cast<unsigned>(LoginFailureReason::ServiceUnavailable) ||
                 !body.contains("error") || !body["error"].is_string())) {
                return;
            }

            LoginInfo info;
            if (!readLoginInfo(body, info)) {
                LogWarn << "invalid login info in notification: " << method << logEnd;
                return;
            }
            info_ = std::move(info);
            if (method == "login.ready") {
                emit loginReady(static_cast<LoginReadySource>(body["source"].get<unsigned>()), info_.uid);
            } else if (method == "login.loggedOut") {
                emit logouted();
            } else if (method == "login.failed") {
                emit loginFailed(static_cast<LoginFailureReason>(body["reason"].get<unsigned>()),
                                 to_u8(body["error"].get<std::string>()));
            }
        },
        [this]() {
            info_ = LoginInfo{LoginFailed};
            emit loginFailed(LoginFailureReason::ServiceUnavailable, u8"Login service unavailable");
        },
        [this]() {
            refresh();
        });
}

LoginLogic::~LoginLogic() {
    LumaClient::instance().removeListener(this);
}

void LoginLogic::autoLogin() {
    post("login.autoLogin");
}
void LoginLogic::login() {
    if (state() == Logining || hasUsableSession()) {
        return;
    }
    if (post("login.start")) {
        info_.state = Logining;
    } else {
        info_ = LoginInfo{LoginFailed};
        emit loginFailed(LoginFailureReason::ServiceUnavailable, u8"Login service unavailable");
    }
}
void LoginLogic::logout() {
    post("login.logout");
}
void LoginLogic::openProfilePage() {
    post("login.openProfile");
}

void LoginLogic::refresh() {
    const auto result =
        LumaClient::instance().call("login.getInfo", nlohmann::json::object(), std::chrono::milliseconds(100));
    LoginInfo info;
    if (result.status != CallStatus::Success || !readLoginInfo(result.body, info)) {
        LogWarn << "refresh login info failed, status=" << static_cast<int>(result.status) << logEnd;
        return;
    }
    info_ = std::move(info);
}
