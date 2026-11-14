// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <memory>

#include <QObject>
#include <QStringList>
#include <QTimer>

#include "feedback_common.h"
#include "ipcwrapper.h"

class FeedbackClient : public QObject, public ISimpleIPCCallback {
    Q_OBJECT

    enum class State {
        Connecting,
        Submitting,
        Cancelling,
        WaitingForClose,
    };

public:
    explicit FeedbackClient(QObject* parent = nullptr);
    ~FeedbackClient() override;

    virtual void start(const QString& description, const QString& email,
                       const QStringList& screenshots, bool includeLogs);
    virtual void cancel();

signals:
    void finished(FeedbackFinishStatus status, const QString& feedbackId,
                  const QString& errorCode, const QString& message);

private:
    void launch();
    void sendCancellation();
    void finishSession();
    void fail(std::u8string errorCode, const QString& message);
    QString localizedResultMessage(const FeedbackFinishedMsg& result) const;
    QString withServerMessage(const QString& summary, const FeedbackFinishedMsg& result) const;
    std::u8string deviceInfo() const;

    void onConnected() override;
    void onClosed() override;
    void onIPCMsg(const BinaryStream& rawMsg) override;

private:
    std::unique_ptr<IPCServer> ipc_;
    QTimer connectionTimer_;
    QTimer cancellationTimer_;
    QTimer closeTimer_;
    SubmitFeedbackMsg request_;
    FeedbackFinishedMsg result_;
    std::u8string key_;
    State state_ = State::Connecting;
};
