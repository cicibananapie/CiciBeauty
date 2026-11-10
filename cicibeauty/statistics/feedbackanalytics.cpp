// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "feedbackanalytics.h"

#include <cassert>

#include "eventid.h"
#include "nlohmann/json.hpp"
#include "cicireport.h"
#include "uuid.h"

namespace {

const char8_t* feedbackActionName(FeedbackUiAction action) {
    switch (action) {
        case FeedbackUiAction::AddScreenshotClick:
            return u8"add_screenshot_click";
        case FeedbackUiAction::RemoveScreenshotClick:
            return u8"remove_screenshot_click";
        case FeedbackUiAction::IncludeLogsToggle:
            return u8"include_logs_toggle";
        case FeedbackUiAction::CancelClick:
            return u8"cancel_click";
        case FeedbackUiAction::CancelSubmissionClick:
            return u8"cancel_submission_click";
    }

    assert(false);
    return u8"unknown";
}

const char8_t* feedbackSubmitResultName(FeedbackSubmitResult result) {
    switch (result) {
        case FeedbackSubmitResult::Succeeded:
            return u8"succeeded";
        case FeedbackSubmitResult::Failed:
            return u8"failed";
        case FeedbackSubmitResult::Cancelled:
            return u8"cancelled";
    }

    assert(false);
    return u8"unknown";
}

nlohmann::json feedbackSubmitProperties(std::u8string_view attemptId,
                                        bool includeLogs,
                                        uint32_t screenshotCount,
                                        bool hasEmail) {
    return nlohmann::json::object({
        {"attempt_id", to_json(std::u8string(attemptId))},
        {"include_logs", includeLogs},
        {"screenshot_count", screenshotCount},
        {"has_email", hasEmail},
    });
}

} // namespace

void reportFeedbackUiAction(FeedbackUiAction action) {
    CiciReport::reportEvent(KEVT_APP_FEEDBACK_UI_ACTION,
                            nlohmann::json::object({
                                {"action", to_json(std::u8string(feedbackActionName(action)))},
                            }));
}

void reportFeedbackIncludeLogsToggle(bool enabled) {
    CiciReport::reportEvent(KEVT_APP_FEEDBACK_UI_ACTION,
                            nlohmann::json::object({
                                {"action", to_json(std::u8string(feedbackActionName(
                                    FeedbackUiAction::IncludeLogsToggle)))},
                                {"enabled", enabled},
                            }));
}

std::u8string reportFeedbackSubmitUiAction(bool includeLogs,
                                           uint32_t screenshotCount,
                                           bool hasEmail) {
    const std::u8string attemptId = CiciUUID::generate();
    auto properties = feedbackSubmitProperties(
        attemptId, includeLogs, screenshotCount, hasEmail);
    properties["action"] = to_json(std::u8string(u8"submit_click"));
    CiciReport::reportEvent(KEVT_APP_FEEDBACK_UI_ACTION, properties);
    return attemptId;
}

void reportFeedbackSubmitResult(std::u8string_view attemptId,
                                FeedbackSubmitResult result,
                                std::u8string_view failureReason,
                                std::u8string_view feedbackId,
                                bool includeLogs,
                                uint32_t screenshotCount,
                                bool hasEmail,
                                uint64_t durationMs) {
    auto properties = feedbackSubmitProperties(
        attemptId, includeLogs, screenshotCount, hasEmail);
    properties["result"] = to_json(std::u8string(feedbackSubmitResultName(result)));
    properties["failure_reason"] = to_json(std::u8string(failureReason));
    properties["feedback_id"] = to_json(std::u8string(feedbackId));
    properties["duration_ms"] = durationMs;
    CiciReport::reportEvent(KEVT_APP_FEEDBACK_SUBMIT_RESULT, properties);
}
