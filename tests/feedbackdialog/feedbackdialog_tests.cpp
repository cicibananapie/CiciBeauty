// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "cicibeauty/feedback/feedbackclient.h"
#include "cicibeauty/feedback/feedbackdialog.h"
#include "cicibeauty/statistics/feedbackanalytics.h"
#include "cicibase.h"
#include "log.h"

#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStringList>
#include <QTextEdit>
#include <QWidget>

void applyCiciWindowIcon(QWidget*) {}

struct ReportedFeedbackUiAction {
	std::u8string action;
	std::optional<bool> enabled;
};

struct ReportedFeedbackSubmitResult {
	std::u8string attemptId;
	FeedbackSubmitResult result;
	std::u8string failureReason;
	std::u8string feedbackId;
	bool includeLogs = false;
	uint32_t screenshotCount = 0;
	bool hasEmail = false;
};

std::vector<ReportedFeedbackUiAction> &reportedFeedbackUiActions() {
	static std::vector<ReportedFeedbackUiAction> actions;
	return actions;
}

std::vector<ReportedFeedbackSubmitResult> &reportedFeedbackSubmitResults() {
	static std::vector<ReportedFeedbackSubmitResult> results;
	return results;
}

void reportFeedbackUiAction(FeedbackUiAction action) {
	std::u8string name;
	switch (action) {
	case FeedbackUiAction::AddScreenshotClick:
		name = u8"add_screenshot_click";
		break;
	case FeedbackUiAction::RemoveScreenshotClick:
		name = u8"remove_screenshot_click";
		break;
	case FeedbackUiAction::IncludeLogsToggle:
		name = u8"include_logs_toggle";
		break;
	case FeedbackUiAction::CancelClick:
		name = u8"cancel_click";
		break;
	case FeedbackUiAction::CancelSubmissionClick:
		name = u8"cancel_submission_click";
		break;
	}
	reportedFeedbackUiActions().push_back({std::move(name), std::nullopt});
}

void reportFeedbackIncludeLogsToggle(bool enabled) {
	reportedFeedbackUiActions().push_back({u8"include_logs_toggle", enabled});
}

std::u8string reportFeedbackSubmitUiAction(bool, uint32_t, bool) {
	reportedFeedbackUiActions().push_back({u8"submit_click", std::nullopt});
	return u8"feedback-attempt";
}

void reportFeedbackSubmitResult(std::u8string_view attemptId,
				FeedbackSubmitResult result,
				std::u8string_view failureReason,
				std::u8string_view feedbackId,
				bool includeLogs,
				uint32_t screenshotCount,
				bool hasEmail,
				uint64_t) {
	reportedFeedbackSubmitResults().push_back({
		std::u8string(attemptId), result, std::u8string(failureReason),
		std::u8string(feedbackId), includeLogs, screenshotCount, hasEmail,
	});
}

namespace {

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void requireEqual(const QString &actual, const QString &expected, const std::string &context) {
	if (actual != expected) {
		throw std::runtime_error(context + ": expected '" + expected.toStdString() + "', got '" +
					 actual.toStdString() + "'");
	}
}

void processEvents() {
	QCoreApplication::processEvents();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QCoreApplication::processEvents();
}

template<typename T> T *requireChild(QObject *parent, const char *objectName) {
	auto *child = parent->findChild<T *>(QString::fromLatin1(objectName));
	require(child != nullptr, std::string("missing child widget: ") + objectName);
	return child;
}

class FakeFeedbackClient final : public FeedbackClient {
public:
	explicit FakeFeedbackClient(QObject *parent) : FeedbackClient(parent) {}

	void start(const QString &description, const QString &email, const QStringList &screenshots,
		   bool includeLogs) override {
		++startCount_;
		description_ = description;
		email_ = email;
		screenshots_ = screenshots;
		includeLogs_ = includeLogs;
	}

	void cancel() override { ++cancelCount_; }

	void complete(FeedbackFinishStatus status, const QString &feedbackId = {},
		      const QString &message = {}, const QString &errorCode = {}) {
		emit finished(status, feedbackId, errorCode, message);
		deleteLater();
	}

	int startCount_ = 0;
	int cancelCount_ = 0;
	QString description_;
	QString email_;
	QStringList screenshots_;
	bool includeLogs_ = false;
};

struct DialogHarness {
	DialogHarness()
		: dialog_(
			  "saved@example.com", &parent_,
			  [this](QObject *owner) {
				  auto *client = new FakeFeedbackClient(owner);
				  clients_.push_back(client);
				  return client;
			  },
			  [this]() {
				  ++screenshotPickerCallCount_;
				  return pickedScreenshots_;
			  }) {
		reportedFeedbackUiActions().clear();
		reportedFeedbackSubmitResults().clear();
		description_ = requireChild<QTextEdit>(&dialog_, "feedbackDescription");
		counter_ = requireChild<QLabel>(&dialog_, "feedbackDescriptionCounter");
		email_ = requireChild<QLineEdit>(&dialog_, "feedbackEmail");
		screenshots_ = requireChild<QListWidget>(&dialog_, "feedbackScreenshots");
		addScreenshot_ = requireChild<QPushButton>(&dialog_, "feedbackAddScreenshot");
		removeScreenshot_ = requireChild<QPushButton>(&dialog_, "feedbackRemoveScreenshot");
		includeLogs_ = requireChild<QCheckBox>(&dialog_, "feedbackIncludeLogs");
		status_ = requireChild<QLabel>(&dialog_, "feedbackStatus");
		submit_ = requireChild<QPushButton>(&dialog_, "feedbackSubmit");
		cancel_ = requireChild<QPushButton>(&dialog_, "feedbackCancel");

		QObject::connect(&dialog_, &QDialog::accepted, [&]() { ++acceptedCount_; });
		QObject::connect(&dialog_, &QDialog::rejected, [&]() { ++rejectedCount_; });
	}

	FakeFeedbackClient *currentClient() const {
		require(!clients_.empty() && clients_.back(), "no active fake feedback client");
		return clients_.back();
	}

	QMessageBox *successMessage() const { return parent_.findChild<QMessageBox *>(); }

	QWidget parent_;
	std::vector<QPointer<FakeFeedbackClient>> clients_;
	QStringList pickedScreenshots_;
	int screenshotPickerCallCount_ = 0;
	FeedbackDialog dialog_;
	QTextEdit *description_ = nullptr;
	QLabel *counter_ = nullptr;
	QLineEdit *email_ = nullptr;
	QListWidget *screenshots_ = nullptr;
	QPushButton *addScreenshot_ = nullptr;
	QPushButton *removeScreenshot_ = nullptr;
	QCheckBox *includeLogs_ = nullptr;
	QLabel *status_ = nullptr;
	QPushButton *submit_ = nullptr;
	QPushButton *cancel_ = nullptr;
	int acceptedCount_ = 0;
	int rejectedCount_ = 0;
};

FakeFeedbackClient *startSubmission(DialogHarness &harness) {
	harness.description_->setPlainText("  feedback body  ");
	harness.email_->setText("  user@example.com  ");
	harness.screenshots_->addItems({"first.png", "second.jpg"});
	harness.includeLogs_->setChecked(true);
	harness.dialog_.show();
	processEvents();

	harness.submit_->click();
	processEvents();

	auto *client = harness.currentClient();
	require(client->startCount_ == 1, "submission should start exactly once");
	requireEqual(client->description_, "feedback body", "trimmed description");
	requireEqual(client->email_, "user@example.com", "trimmed email");
	require(client->screenshots_ == QStringList({"first.png", "second.jpg"}),
		"submission should include all screenshot paths");
	require(client->includeLogs_, "submission should include logs when requested");
	require(!harness.dialog_.isVisible(), "dialog should hide while submitting");
	require(!harness.description_->isEnabled(), "description should be disabled while submitting");
	require(!harness.submit_->isEnabled(), "submit should be disabled while submitting");
	require(harness.cancel_->isEnabled(), "submission cancellation should be enabled");
	requireEqual(harness.cancel_->text(), "Cancel submission", "submitting cancel label");
	return client;
}

void requestCancellation(DialogHarness &harness) {
	harness.dialog_.show();
	processEvents();
	harness.cancel_->click();
	processEvents();

	require(harness.currentClient()->cancelCount_ == 1, "cancel should be requested exactly once");
	require(!harness.dialog_.isVisible(), "dialog should hide while cancellation is pending");
	require(!harness.cancel_->isEnabled(), "cancel button should be disabled while cancelling");
	requireEqual(harness.cancel_->text(), "Cancelling...", "cancelling label");
}

void closeSuccessMessage(DialogHarness &harness) {
	auto *messageBox = harness.successMessage();
	require(messageBox != nullptr, "successful submission should show a message box");
	messageBox->close();
	processEvents();
}

void testInitialEditingControls() {
	DialogHarness harness;

	requireEqual(harness.email_->text(), "saved@example.com", "initial email");
	requireEqual(harness.counter_->text(), "0 / 3000", "initial description counter");
	require(!harness.submit_->isEnabled(), "submit should be disabled without a description");
	require(harness.addScreenshot_->isEnabled(), "screenshots should initially be addable");
	require(!harness.removeScreenshot_->isEnabled(), "remove should require a selected screenshot");
	requireEqual(harness.cancel_->text(), "Cancel", "editing cancel label");
	harness.email_->setText(QString(300, 'e'));
	require(harness.email_->text().size() == 254, "email should be limited to 254 characters");

	harness.description_->setPlainText(QString(3001, 'x'));
	processEvents();
	require(harness.description_->toPlainText().size() == 3000,
		"description should be truncated to 3000 characters");
	requireEqual(harness.counter_->text(), "3000 / 3000", "truncated description counter");
	require(harness.submit_->isEnabled(), "non-empty description should enable submit");

	harness.pickedScreenshots_ = {"one.png", "two.png", "three.png", "four.png"};
	harness.addScreenshot_->click();
	processEvents();
	require(harness.screenshotPickerCallCount_ == 1, "Add should open the screenshot picker once");
	require(harness.screenshots_->count() == 3, "Add should keep at most three selected screenshots");
	requireEqual(harness.screenshots_->item(2)->text(), "three.png", "third selected screenshot");

	harness.screenshots_->setCurrentRow(0);
	processEvents();
	require(!harness.addScreenshot_->isEnabled(), "three screenshots should disable Add");
	require(harness.removeScreenshot_->isEnabled(), "a selected screenshot should enable Remove");
	harness.removeScreenshot_->click();
	processEvents();
	require(harness.screenshots_->count() == 2, "Remove should delete the selected screenshot");
	require(harness.addScreenshot_->isEnabled(), "removing a screenshot should re-enable Add");

	harness.screenshots_->setCurrentRow(0);
	harness.removeScreenshot_->click();
	harness.pickedScreenshots_ = {"three.png", "four.png"};
	harness.addScreenshot_->click();
	processEvents();
	require(harness.screenshots_->count() == 2, "Add should ignore an existing screenshot path");
	requireEqual(harness.screenshots_->item(1)->text(), "four.png", "unique screenshot after duplicate");

	harness.pickedScreenshots_ = {"five.png", "six.png"};
	harness.addScreenshot_->click();
	processEvents();
	require(harness.screenshots_->count() == 3, "Add should fill only the remaining screenshot slot");
	requireEqual(harness.screenshots_->item(2)->text(), "five.png", "screenshot in the final available slot");
	require(!harness.addScreenshot_->isEnabled(), "a full screenshot list should disable Add");

	harness.dialog_.show();
	harness.cancel_->click();
	processEvents();
	require(harness.rejectedCount_ == 1, "editing Cancel should reject the dialog");
	require(!harness.dialog_.isVisible(), "editing Cancel should close the dialog");
}

void testUiActionReporting() {
	DialogHarness harness;
	harness.dialog_.show();
	processEvents();

	harness.pickedScreenshots_ = {"one.png"};
	harness.addScreenshot_->click();
	processEvents();
	harness.screenshots_->setCurrentRow(0);
	harness.removeScreenshot_->click();
	harness.includeLogs_->click();
	harness.description_->setPlainText("feedback body");
	harness.submit_->click();
	processEvents();

	harness.dialog_.show();
	processEvents();
	harness.cancel_->click();
	processEvents();

	const auto &actions = reportedFeedbackUiActions();
	require(actions.size() == 5, "feedback UI actions were not reported exactly once");
	require(actions[0].action == u8"add_screenshot_click",
		"Add screenshot reported the wrong UI action");
	require(actions[1].action == u8"remove_screenshot_click",
		"Remove screenshot reported the wrong UI action");
	require(actions[2].action == u8"include_logs_toggle",
		"Include logs reported the wrong UI action");
	require(actions[2].enabled.has_value() && *actions[2].enabled,
		"Include logs did not report the checked value");
	require(actions[3].action == u8"submit_click",
		"Submit reported the wrong UI action");
	require(actions[4].action == u8"cancel_submission_click",
		"Cancel submission reported the wrong UI action");

	DialogHarness closeHarness;
	closeHarness.dialog_.show();
	closeHarness.cancel_->click();
	processEvents();
	require(reportedFeedbackUiActions().size() == 1,
		"editing Close was not reported exactly once");
	require(reportedFeedbackUiActions()[0].action == u8"cancel_click",
		"editing Cancel reported the wrong UI action");
}

void testCloseWhileSubmittingKeepsSubmissionAlive() {
	DialogHarness harness;
	auto *client = startSubmission(harness);

	harness.dialog_.show();
	processEvents();
	harness.dialog_.close();
	processEvents();

	require(!harness.dialog_.isVisible(), "X or Esc should hide a reopened submitting dialog");
	require(harness.rejectedCount_ == 0, "hiding during submission should not reject the dialog");
	require(client->cancelCount_ == 0, "X or Esc should not cancel the active submission");
	require(harness.currentClient() == client, "active client should survive a submitting dialog close");
}

void testCloseWhileCancellingKeepsCancellationAlive() {
	DialogHarness harness;
	auto *client = startSubmission(harness);
	requestCancellation(harness);

	harness.dialog_.show();
	processEvents();
	QKeyEvent escapeKey(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
	QCoreApplication::sendEvent(&harness.dialog_, &escapeKey);
	processEvents();

	require(!harness.dialog_.isVisible(), "X or Esc should hide a reopened cancelling dialog");
	require(harness.rejectedCount_ == 0, "hiding during cancellation should not reject the dialog");
	require(client->cancelCount_ == 1, "closing should not send a second cancellation request");

	client->complete(FeedbackFinishStatus::Cancelled);
	processEvents();
	require(harness.rejectedCount_ == 1, "completed cancellation should reject the dialog");
}

void testRetryAfterFailure() {
	DialogHarness harness;
	auto *client = startSubmission(harness);
	client->complete(FeedbackFinishStatus::Failed, {}, "Temporary failure", "network_error");
	processEvents();
	require(reportedFeedbackSubmitResults().size() == 1,
		"failed submission should report exactly one result");
	require(reportedFeedbackSubmitResults()[0].result == FeedbackSubmitResult::Failed,
		"failed submission reported the wrong result");
	require(reportedFeedbackSubmitResults()[0].failureReason == u8"network_error",
		"failed submission did not report its stable error code");

	require(harness.dialog_.isVisible(), "ordinary failure should restore the dialog");
	requireEqual(harness.status_->text(), "Temporary failure", "failure status");
	requireEqual(harness.submit_->text(), "Retry", "failure submit label");
	require(harness.description_->isEnabled(), "failure should restore editing controls");
	require(harness.submit_->isEnabled(), "failure should allow retrying the submission");
	require(harness.acceptedCount_ == 0 && harness.rejectedCount_ == 0,
		"ordinary failure should keep the dialog open");

	harness.submit_->click();
	processEvents();
	require(harness.clients_.size() == 2, "Retry should create a new feedback client");
	require(harness.currentClient()->startCount_ == 1, "Retry should start the new client");
	require(!harness.dialog_.isVisible(), "Retry should hide the dialog again");
}

void testDefaultFailureMessage() {
	DialogHarness harness;
	auto *client = startSubmission(harness);
	client->complete(FeedbackFinishStatus::Failed);
	processEvents();

	requireEqual(harness.status_->text(), "Feedback could not be sent. Please try again.",
		     "default failure status");
	require(harness.dialog_.isVisible(), "failure without a message should restore the dialog");
}

void testCompletionMatrix() {
	struct CompletionCase {
		const char *name;
		FeedbackFinishStatus status;
		bool cancellationRequested;
	};

	const std::vector<CompletionCase> cases{
		{"succeeded", FeedbackFinishStatus::Succeeded, false},
		{"failed", FeedbackFinishStatus::Failed, false},
		{"cancelled", FeedbackFinishStatus::Cancelled, false},
		{"succeeded after cancellation", FeedbackFinishStatus::Succeeded, true},
		{"failed after cancellation", FeedbackFinishStatus::Failed, true},
		{"cancelled after cancellation", FeedbackFinishStatus::Cancelled, true},
	};

	for (const auto &testCase : cases) {
		DialogHarness harness;
		auto *client = startSubmission(harness);
		if (testCase.cancellationRequested) {
			requestCancellation(harness);
		}

		const QString feedbackId = testCase.cancellationRequested ? QString() : QString("FB-42");
		client->complete(testCase.status, feedbackId, "matrix failure", "server_error");
		processEvents();
		const std::string context = testCase.name;
		require(reportedFeedbackSubmitResults().size() == 1,
			context + ": submission result was not reported exactly once");
		const auto &reported = reportedFeedbackSubmitResults()[0];
		require(reported.attemptId == u8"feedback-attempt",
			context + ": result did not retain the submit attempt id");
		require(reported.includeLogs && reported.screenshotCount == 2 && reported.hasEmail,
			context + ": result did not retain submission metadata");
		if (testCase.status == FeedbackFinishStatus::Succeeded) {
			require(reported.feedbackId == (feedbackId.isEmpty() ? u8"" : u8"FB-42"),
				context + ": success did not report the feedback id");
		}

		if (testCase.status == FeedbackFinishStatus::Succeeded) {
			require(harness.acceptedCount_ == 1, context + ": success should accept the dialog");
			require(harness.rejectedCount_ == 0, context + ": success should not reject the dialog");
			auto *messageBox = harness.successMessage();
			require(messageBox != nullptr, context + ": success should show a message box");
			requireEqual(messageBox->text(),
				     feedbackId.isEmpty() ? "Thank you. Your feedback was sent."
							  : "Thank you. Feedback ID: FB-42",
				     context + ": success message");
			closeSuccessMessage(harness);
		} else if (testCase.status == FeedbackFinishStatus::Failed && !testCase.cancellationRequested) {
			require(harness.acceptedCount_ == 0 && harness.rejectedCount_ == 0,
				context + ": ordinary failure should leave the dialog active");
			require(harness.dialog_.isVisible(), context + ": ordinary failure should restore the dialog");
			requireEqual(harness.status_->text(), "matrix failure", context + ": failure message");
			require(harness.successMessage() == nullptr,
				context + ": ordinary failure should not show a success message");
		} else {
			require(harness.acceptedCount_ == 0, context + ": non-success should not accept the dialog");
			require(harness.rejectedCount_ == 1,
				context + ": cancellation result should reject the dialog");
			require(!harness.dialog_.isVisible(),
				context + ": cancellation result should keep the dialog hidden");
			require(harness.successMessage() == nullptr,
				context + ": cancellation result should not show a success message");
		}
	}
}

} // namespace

int main(int argc, char *argv[]) {
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
		qputenv("QT_QPA_PLATFORM", "minimal");
	}
	QApplication application(argc, argv);
	initCiciLogConfig(ciciDefaultLogLevel(), u8"feedbackdialog_tests.log");
	if (!initCiciBase()) {
		std::cerr << "Failed to initialize the main-thread dispatcher\n";
		return 1;
	}

	const std::vector<std::pair<const char *, std::function<void()>>> tests{
		{"initial editing controls", testInitialEditingControls},
		{"UI action reporting", testUiActionReporting},
		{"close while submitting", testCloseWhileSubmittingKeepsSubmissionAlive},
		{"close while cancelling", testCloseWhileCancellingKeepsCancellationAlive},
		{"retry after failure", testRetryAfterFailure},
		{"default failure message", testDefaultFailureMessage},
		{"completion matrix", testCompletionMatrix},
	};

	int failures = 0;
	for (const auto &[name, test] : tests) {
		try {
			test();
			processEvents();
			std::cout << "PASS: " << name << '\n';
		} catch (const std::exception &error) {
			++failures;
			std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
		}
	}

	if (failures != 0) {
		std::cerr << failures << " feedback dialog test(s) failed\n";
		return 1;
	}

	std::cout << "All feedback dialog tests passed\n";
	return 0;
}
