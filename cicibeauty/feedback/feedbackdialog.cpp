// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "feedbackdialog.h"

#include <obs.h>
#include <QCheckBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSysInfo>
#include <QTextEdit>
#include <QVBoxLayout>

#include <utility>

#include "feedbackclient.h"
#include "../qstringconv.h"
#include "../statistics/feedbackanalytics.h"
#include "../ui/commonui.h"

namespace {

FeedbackSubmitResult feedbackSubmitResult(FeedbackFinishStatus status) {
    switch (status) {
        case FeedbackFinishStatus::Succeeded:
            return FeedbackSubmitResult::Succeeded;
        case FeedbackFinishStatus::Failed:
            return FeedbackSubmitResult::Failed;
        case FeedbackFinishStatus::Cancelled:
            return FeedbackSubmitResult::Cancelled;
    }

    Q_ASSERT(false);
    return FeedbackSubmitResult::Failed;
}

} // namespace

FeedbackDialog::FeedbackDialog(const QString& email, QWidget* parent)
    : FeedbackDialog(email, parent, [](QObject* owner) { return new FeedbackClient(owner); }) {}

FeedbackDialog::FeedbackDialog(const QString& email, QWidget* parent, ClientFactory clientFactory,
                               ScreenshotPicker screenshotPicker)
    : QDialog(parent), clientFactory_(std::move(clientFactory)), screenshotPicker_(std::move(screenshotPicker)) {
    Q_ASSERT(clientFactory_);
    if (!screenshotPicker_) {
        screenshotPicker_ = [this]() {
            return QFileDialog::getOpenFileNames(
                this, tr("Select Screenshots"), {},
                tr("Images (*.png *.jpg *.jpeg *.webp *.gif)"));
        };
    }
    setWindowTitle(tr("Cici Beauty Feedback"));
    applyCiciWindowIcon(this);
    setModal(false);
    resize(560, 600);

    auto* layout = new QVBoxLayout(this);
    auto* descriptionLabel = new QLabel(tr("Description *"), this);
    layout->addWidget(descriptionLabel);
    description_ = new QTextEdit(this);
    description_->setObjectName("feedbackDescription");
    description_->setPlaceholderText(tr("Tell us what happened or what you would like improved."));
    description_->setMinimumHeight(150);
    layout->addWidget(description_);
    counter_ = new QLabel(tr("0 / 3000"), this);
    counter_->setObjectName("feedbackDescriptionCounter");
    counter_->setAlignment(Qt::AlignRight);
    layout->addWidget(counter_);

    auto* form = new QFormLayout;
    email_ = new QLineEdit(email, this);
    email_->setObjectName("feedbackEmail");
    email_->setMaxLength(254);
    email_->setPlaceholderText(tr("Optional"));
    form->addRow(tr("Email"), email_);
    layout->addLayout(form);

    auto* screenshotLabel = new QLabel(tr("Screenshots (optional, up to 3)"), this);
    layout->addWidget(screenshotLabel);
    screenshots_ = new QListWidget(this);
    screenshots_->setObjectName("feedbackScreenshots");
    screenshots_->setMaximumHeight(105);
    layout->addWidget(screenshots_);
    auto* screenshotButtons = new QHBoxLayout;
    addScreenshot_ = new QPushButton(tr("Add..."), this);
    addScreenshot_->setObjectName("feedbackAddScreenshot");
    removeScreenshot_ = new QPushButton(tr("Remove"), this);
    removeScreenshot_->setObjectName("feedbackRemoveScreenshot");
    screenshotButtons->addWidget(addScreenshot_);
    screenshotButtons->addWidget(removeScreenshot_);
    screenshotButtons->addStretch();
    layout->addLayout(screenshotButtons);

    includeLogs_ = new QCheckBox(tr("Include Cici Beauty logs"), this);
    includeLogs_->setObjectName("feedbackIncludeLogs");
    includeLogs_->setChecked(false);
    layout->addWidget(includeLogs_);
    auto* privacy = new QLabel(
        tr("Logs may contain device and operation details. They do not include your login token or crash dumps."), this);
    privacy->setWordWrap(true);
    layout->addWidget(privacy);

    const char* obsVersion = obs_get_version_string();
    auto* systemInfo = new QLabel(
        tr("System: %1 (%2)\nOBS: %3\nQt: %4")
            .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(),
                 QString::fromUtf8(obsVersion ? obsVersion : ""), QString::fromLatin1(qVersion())),
        this);
    systemInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(systemInfo);

    status_ = new QLabel(this);
    status_->setObjectName("feedbackStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    layout->addStretch();

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    cancel_ = new QPushButton(tr("Cancel"), this);
    cancel_->setObjectName("feedbackCancel");
    submit_ = new QPushButton(tr("Submit"), this);
    submit_->setObjectName("feedbackSubmit");
    submit_->setDefault(true);
    buttons->addWidget(cancel_);
    buttons->addWidget(submit_);
    layout->addLayout(buttons);

    connect(description_, &QTextEdit::textChanged, this, [this]() {
        QString text = description_->toPlainText();
        if (text.size() > 3000) {
            text.truncate(3000);
            description_->setPlainText(text);
            auto cursor = description_->textCursor();
            cursor.movePosition(QTextCursor::End);
            description_->setTextCursor(cursor);
        }
        counter_->setText(tr("%1 / 3000").arg(text.size()));
        updateControls();
    });
    connect(screenshots_, &QListWidget::itemSelectionChanged, this, &FeedbackDialog::updateControls);
    connect(addScreenshot_, &QPushButton::clicked, this, [this]() {
        reportFeedbackUiAction(FeedbackUiAction::AddScreenshotClick);
        addScreenshots();
    });
    connect(removeScreenshot_, &QPushButton::clicked, this, [this]() {
        reportFeedbackUiAction(FeedbackUiAction::RemoveScreenshotClick);
        removeScreenshot();
    });
    connect(includeLogs_, &QCheckBox::clicked, this, [](bool checked) {
        reportFeedbackIncludeLogsToggle(checked);
    });
    connect(submit_, &QPushButton::clicked, this, [this]() {
        submit();
    });
    connect(cancel_, &QPushButton::clicked, this, [this]() {
        reportFeedbackUiAction(state_ == State::Editing
                                   ? FeedbackUiAction::CancelClick
                                   : FeedbackUiAction::CancelSubmissionClick);
        cancelOrClose();
    });
    updateControls();
}

void FeedbackDialog::addScreenshots() {
    const int remaining = 3 - screenshots_->count();
    if (remaining <= 0) {
        return;
    }

    const QStringList selected = screenshotPicker_();
    for (const auto& path : selected.mid(0, remaining)) {
        bool exists = false;
        for (int i = 0; i < screenshots_->count(); ++i) {
            exists = exists || screenshots_->item(i)->text() == path;
        }
        if (!exists) {
            screenshots_->addItem(path);
        }
    }
    updateControls();
}

void FeedbackDialog::removeScreenshot() {
    delete screenshots_->takeItem(screenshots_->currentRow());
    updateControls();
}

void FeedbackDialog::submit() {
    if (client_) {
        return;
    }

    const QString description = description_->toPlainText().trimmed();
    if (description.isEmpty()) {
        QMessageBox::warning(this, tr("Feedback"), tr("Please enter a description."));
        return;
    }

    QStringList paths;
    for (int i = 0; i < screenshots_->count(); ++i) {
        paths.push_back(screenshots_->item(i)->text());
    }

    auto* client = clientFactory_(this);
    Q_ASSERT(client);
    client_ = client;
    connect(client, &FeedbackClient::finished, this, &FeedbackDialog::onFeedbackFinished);
    submitIncludedLogs_ = includeLogs_->isChecked();
    submitScreenshotCount_ = static_cast<uint32_t>(paths.size());
    submitHadEmail_ = !email_->text().trimmed().isEmpty();
    submitAttemptId_ = reportFeedbackSubmitUiAction(
        submitIncludedLogs_, submitScreenshotCount_, submitHadEmail_);
    submitStartedAt_ = std::chrono::steady_clock::now();
    setSubmitting();
    client->start(description, email_->text().trimmed(), paths, submitIncludedLogs_);
    hide();
}

void FeedbackDialog::cancelOrClose() {
    if (state_ == State::Editing) {
        QDialog::reject();
        return;
    }
    Q_ASSERT(state_ == State::Submitting);
    Q_ASSERT(client_);

    setCancelling();
    hide();
    client_->cancel();
}

void FeedbackDialog::setSubmitting() {
    state_ = State::Submitting;
    status_->setText(tr("Sending feedback..."));
    submit_->setText(tr("Submit"));
    updateControls();
}

void FeedbackDialog::setCancelling() {
    state_ = State::Cancelling;
    status_->setText(tr("Cancelling..."));
    updateControls();
}

void FeedbackDialog::setFailed(const QString& message) {
    state_ = State::Editing;
    status_->setText(message.isEmpty()
        ? tr("Feedback could not be sent. Please try again.")
        : message);
    submit_->setText(tr("Retry"));
    updateControls();
}

void FeedbackDialog::showSuccessMessage(const QString& feedbackId) {
    if (QCoreApplication::closingDown()) {
        return;
    }

    const QString message = feedbackId.isEmpty()
        ? tr("Thank you. Your feedback was sent.")
        : tr("Thank you. Feedback ID: %1").arg(feedbackId);
    auto* messageBox = new QMessageBox(QMessageBox::Information, tr("Feedback"), message,
                                       QMessageBox::Ok, parentWidget());
    messageBox->setAttribute(Qt::WA_DeleteOnClose);
    messageBox->setWindowModality(Qt::WindowModal);
    messageBox->open();
}

void FeedbackDialog::onFeedbackFinished(FeedbackFinishStatus status,
                                        const QString& feedbackId,
                                        const QString& errorCode,
                                        const QString& message) {
    const bool cancellationRequested = state_ == State::Cancelling;
    client_ = nullptr;

    if (!submitAttemptId_.empty()) {
        const auto durationMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - submitStartedAt_).count());
        const auto failureReason = status == FeedbackFinishStatus::Failed
            ? (errorCode.isEmpty() ? std::u8string(u8"unknown") : to_u8(errorCode))
            : std::u8string();
        reportFeedbackSubmitResult(
            submitAttemptId_, feedbackSubmitResult(status), failureReason,
            to_u8(feedbackId), submitIncludedLogs_, submitScreenshotCount_,
            submitHadEmail_, durationMs);
        submitAttemptId_.clear();
    }

    switch (status) {
    case FeedbackFinishStatus::Succeeded:
        showSuccessMessage(feedbackId);
        QDialog::accept();
        break;
    case FeedbackFinishStatus::Failed:
        if (cancellationRequested) {
            QDialog::reject();
        } else {
            setFailed(message);
            show();
            raise();
            activateWindow();
        }
        break;
    case FeedbackFinishStatus::Cancelled:
        QDialog::reject();
        break;
    }
}

void FeedbackDialog::reject() {
    // Submission continues after the dialog is hidden. If the user reopens it and presses X or Esc,
    // hide it again instead of destroying the active client.
    if (state_ == State::Submitting || state_ == State::Cancelling) {
        hide();
        return;
    }
    QDialog::reject();
}

void FeedbackDialog::updateControls() {
    const bool editable = state_ == State::Editing;
    description_->setEnabled(editable);
    email_->setEnabled(editable);
    screenshots_->setEnabled(editable);
    includeLogs_->setEnabled(editable);
    addScreenshot_->setEnabled(editable && screenshots_->count() < 3);
    removeScreenshot_->setEnabled(editable && screenshots_->currentItem() != nullptr);
    submit_->setEnabled(editable && !description_->toPlainText().trimmed().isEmpty());
    cancel_->setEnabled(state_ != State::Cancelling);
    cancel_->setText(state_ == State::Submitting
        ? tr("Cancel submission")
        : state_ == State::Cancelling ? tr("Cancelling...") : tr("Cancel"));
}
