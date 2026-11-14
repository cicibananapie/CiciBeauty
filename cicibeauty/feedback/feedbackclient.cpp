// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "feedbackclient.h"

#include <algorithm>

#include <obs.h>
#include <QLocale>
#include <QSysInfo>

#include "../qstringconv.h"
#include "../lumaclient.h"
#include "cicibeautyconfigure.h"
#include "log.h"
#include "misc.h"
#include "nlohmann/json.hpp"
#include "uuid.h"

namespace {

bool isServerErrorCode(std::u8string_view code) {
    return !code.empty() && std::all_of(code.begin(), code.end(), [](char8_t value) {
        return (value >= u8'A' && value <= u8'Z') ||
               (value >= u8'0' && value <= u8'9') || value == u8'_';
    });
}

}  // namespace

FeedbackClient::FeedbackClient(QObject* parent) : QObject(parent) {
    connectionTimer_.setSingleShot(true);
    connect(&connectionTimer_, &QTimer::timeout, this, [this]() {
        fail(u8"helper_start_timeout", tr("The feedback service did not start."));
    });

    cancellationTimer_.setSingleShot(true);
    connect(&cancellationTimer_, &QTimer::timeout, this, [this]() {
        fail(u8"cancel_timeout", tr("Could not cancel feedback submission."));
    });

    closeTimer_.setSingleShot(true);
    connect(&closeTimer_, &QTimer::timeout, this, [this]() {
        LogWarn << "feedback helper close timeout, finishing session locally" << logEnd;
        finishSession();
    });
}

FeedbackClient::~FeedbackClient() {
    if (ipc_) {
        ipc_->removeCallback(this);
    }
}

void FeedbackClient::start(const QString& description, const QString& email,
                           const QStringList& screenshots, bool includeLogs) {
    LogImpl::instance().flush();
    request_.description_ = to_u8(description);
    request_.email_ = to_u8(email);
    for (const auto& path : screenshots) {
        request_.screenshotPaths_.push_back(to_u8(path));
    }
    request_.deviceInfo_ = deviceInfo();
    request_.includeLogs_ = includeLogs;
    LogInfo << "starting feedback session, screenshots=" << request_.screenshotPaths_.size()
            << " include_logs=" << request_.includeLogs_ << logEnd;

    key_ = CiciUUID::generate();
    ipc_ = std::make_unique<IPCServer>(key_.c_str());
    ipc_->addCallback(this);
    ipc_->start();
    launch();
    connectionTimer_.start(15 * 1000);
}

void FeedbackClient::cancel() {
    if (state_ != State::Connecting && state_ != State::Submitting) {
        LogWarn << "ignore feedback cancellation, state=" << static_cast<int>(state_) << logEnd;
        return;
    }

    LogInfo << "feedback cancellation requested, state=" << static_cast<int>(state_) << logEnd;
    if (state_ == State::Submitting) {
        state_ = State::Cancelling;
        sendCancellation();
        return;
    }
    state_ = State::Cancelling;
}

void FeedbackClient::onConnected() {
    if (state_ != State::Connecting && state_ != State::Cancelling) {
        LogWarn << "feedback helper connected in state=" << static_cast<int>(state_) << logEnd;
        return;
    }

    LogInfo << "feedback helper connected" << logEnd;
    connectionTimer_.stop();
    if (!ipc_->postMsg(request_)) {
        fail(u8"ipc_submit_failed", tr("Could not send feedback to the helper process."));
        return;
    }
    LogInfo << "feedback submission message sent" << logEnd;

    if (state_ == State::Cancelling) {
        sendCancellation();
        return;
    }
    state_ = State::Submitting;
}

void FeedbackClient::sendCancellation() {
    CancelFeedbackMsg cancelMessage;
    if (!ipc_->postMsg(cancelMessage)) {
        fail(u8"ipc_cancel_failed", tr("Could not cancel feedback submission."));
        return;
    }
    LogInfo << "feedback cancellation message sent" << logEnd;
    cancellationTimer_.start(15 * 1000);
}

void FeedbackClient::onClosed() {
    if (state_ == State::WaitingForClose) {
        LogInfo << "feedback helper closed after result" << logEnd;
        QTimer::singleShot(0, this, [this]() {
            finishSession();
        });
        return;
    }

    fail(u8"helper_exited", tr("The feedback service exited unexpectedly."));
}

void FeedbackClient::onIPCMsg(const BinaryStream& rawMsg) {
    const uint32_t id = BinaryUnpack::whitchMsg(rawMsg);
    if (id != kMsgFeedbackFinished) {
        LogWarn << "unexpected feedback IPC message, id=" << id << logEnd;
        return;
    }
    if (state_ != State::Submitting && state_ != State::Cancelling) {
        LogWarn << "feedback result received in state=" << static_cast<int>(state_) << logEnd;
        return;
    }

    FeedbackFinishedMsg result;
    if (!result.fromBinary(rawMsg)) {
        fail(u8"invalid_ipc_result", tr("The feedback service returned an invalid result."));
        return;
    }
    LogInfo << "feedback result received, status=" << static_cast<int>(result.status_)
            << " error_code=" << result.errorCode_ << logEnd;

    FeedbackFinishedAckMsg ack;
    if (!ipc_->postMsg(ack)) {
        LogError << "send feedback result ack failed" << logEnd;
    } else {
        LogInfo << "feedback result ack sent" << logEnd;
    }
    result_ = std::move(result);
    state_ = State::WaitingForClose;
    cancellationTimer_.stop();
    closeTimer_.start(12 * 1000);
}

void FeedbackClient::finishSession() {
    connectionTimer_.stop();
    cancellationTimer_.stop();
    closeTimer_.stop();
    if (!ipc_) {
        return;
    }

    LogInfo << "finishing feedback session, status=" << static_cast<int>(result_.status_)
            << logEnd;
    ipc_->removeCallback(this);
    ipc_.reset();

    const auto result = std::move(result_);
    emit finished(result.status_, from_u8(result.feedbackId_), from_u8(result.errorCode_),
                  localizedResultMessage(result));
    deleteLater();
}

QString FeedbackClient::localizedResultMessage(const FeedbackFinishedMsg& result) const {
    if (result.errorCode_.empty()) {
        return from_u8(result.message_);
    }
    if (result.errorCode_ == u8"invalid_input") {
        return tr("Invalid feedback content.");
    }
    if (result.errorCode_ == u8"invalid_screenshot") {
        return tr("A screenshot is missing or invalid.");
    }
    if (result.errorCode_ == u8"logs_unavailable") {
        return tr("No log files are available.");
    }
    if (result.errorCode_ == u8"log_archive_failed") {
        return tr("Could not prepare logs.");
    }
    if (result.errorCode_ == u8"log_too_large") {
        return tr("The log archive exceeds 50 MB.");
    }
    if (result.errorCode_ == u8"invalid_response") {
        return tr("The server returned an invalid response.");
    }
    if (result.errorCode_ == u8"INVALID_REQUEST") {
        return withServerMessage(
            tr("The feedback information is invalid. Please check it and try again."), result);
    }
    if (result.errorCode_ == u8"FILE_TOO_LARGE") {
        return withServerMessage(
            tr("An attachment is too large. Please remove it or choose a smaller file."), result);
    }
    if (result.errorCode_ == u8"RATE_LIMITED") {
        return withServerMessage(
            tr("Too many feedback submissions. Please try again later."), result);
    }
    if (result.errorCode_ == u8"http_error" && !result.message_.empty()) {
        return from_u8(result.message_);
    }
    if (isServerErrorCode(result.errorCode_) && !result.message_.empty()) {
        return from_u8(result.message_);
    }
    return tr("Feedback could not be sent. Please try again.");
}

QString FeedbackClient::withServerMessage(const QString& summary,
                                          const FeedbackFinishedMsg& result) const {
    const QString message = from_u8(result.message_).trimmed();
    if (message.isEmpty() || message.compare(summary, Qt::CaseInsensitive) == 0) {
        return summary;
    }
    return summary + QStringLiteral("\n") + tr("Server message: %1").arg(message);
}

void FeedbackClient::fail(std::u8string errorCode, const QString& message) {
    LogWarn << "feedback session failed, state=" << static_cast<int>(state_)
            << " reason=" << to_u8(message) << logEnd;
    connectionTimer_.stop();
    cancellationTimer_.stop();
    closeTimer_.stop();
    result_ = {};
    result_.errorCode_ = std::move(errorCode);
    result_.message_ = to_u8(message);
    state_ = State::WaitingForClose;
    QTimer::singleShot(0, this, [this]() {
        finishSession();
    });
}

std::u8string FeedbackClient::deviceInfo() const {
    const nlohmann::json info{
        {"os", to_char(to_u8(QSysInfo::prettyProductName()))},
        {"platform_type", to_char(to_u8(QSysInfo::productType()))},
        {"platform_version", to_char(to_u8(QSysInfo::productVersion()))},
        {"arch", to_char(to_u8(QSysInfo::currentCpuArchitecture()))},
        {"locale", to_char(to_u8(QLocale().name()))},
        {"qt_version", qVersion()},
        {"host_app", "obs"},
        {"obs_version", obs_get_version_string() ? obs_get_version_string() : ""},
        {"channel_id", to_char(std::u8string(CICI_CHANNEL_ID))},
    };
    return to_u8(info.dump());
}

void FeedbackClient::launch() {
    LumaClient::instance().callAsync(this, "helpers.startFeedback", {{"ipcName", to_char(key_)}},
        [this](cici::protocol::CallResult result) {
            if (!result.ok() && (state_ == State::Connecting || state_ == State::Cancelling) && !ipc_->connected()) {
                fail(u8"helper_launch_failed", tr("The feedback service could not be started."));
            }
        });
}
