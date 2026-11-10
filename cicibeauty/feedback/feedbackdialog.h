// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include <QDialog>
#include <QPointer>
#include <QStringList>

#include "feedback_common.h"

class FeedbackClient;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTextEdit;

class FeedbackDialog : public QDialog {
    Q_OBJECT

public:
    using ClientFactory = std::function<FeedbackClient*(QObject*)>;
    using ScreenshotPicker = std::function<QStringList()>;

private:
    enum class State {
        Editing,
        Submitting,
        Cancelling,
    };

public:
    explicit FeedbackDialog(const QString& email, QWidget* parent = nullptr);
    FeedbackDialog(const QString& email, QWidget* parent, ClientFactory clientFactory,
                   ScreenshotPicker screenshotPicker = {});

protected:
    void reject() override;

private:
    void addScreenshots();
    void removeScreenshot();
    void submit();
    void cancelOrClose();
    void setSubmitting();
    void setCancelling();
    void setFailed(const QString& message);
    void showSuccessMessage(const QString& feedbackId);
    void onFeedbackFinished(FeedbackFinishStatus status, const QString& feedbackId,
                            const QString& errorCode, const QString& message);
    void updateControls();

private:
    ClientFactory clientFactory_;
    ScreenshotPicker screenshotPicker_;
    QPointer<FeedbackClient> client_;
    QTextEdit* description_ = nullptr;
    QLabel* counter_ = nullptr;
    QLineEdit* email_ = nullptr;
    QListWidget* screenshots_ = nullptr;
    QPushButton* addScreenshot_ = nullptr;
    QPushButton* removeScreenshot_ = nullptr;
    QCheckBox* includeLogs_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* submit_ = nullptr;
    QPushButton* cancel_ = nullptr;
    State state_ = State::Editing;
    std::u8string submitAttemptId_;
    std::chrono::steady_clock::time_point submitStartedAt_;
    uint32_t submitScreenshotCount_ = 0;
    bool submitIncludedLogs_ = false;
    bool submitHadEmail_ = false;
};
