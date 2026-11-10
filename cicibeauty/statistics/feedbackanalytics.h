// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

enum class FeedbackUiAction {
    AddScreenshotClick,
    RemoveScreenshotClick,
    IncludeLogsToggle,
    CancelClick,
    CancelSubmissionClick,
};

enum class FeedbackSubmitResult {
    Succeeded,
    Failed,
    Cancelled,
};

void reportFeedbackUiAction(FeedbackUiAction action);
void reportFeedbackIncludeLogsToggle(bool enabled);
std::u8string reportFeedbackSubmitUiAction(bool includeLogs,
                                           uint32_t screenshotCount,
                                           bool hasEmail);
void reportFeedbackSubmitResult(std::u8string_view attemptId,
                                FeedbackSubmitResult result,
                                std::u8string_view failureReason,
                                std::u8string_view feedbackId,
                                bool includeLogs,
                                uint32_t screenshotCount,
                                bool hasEmail,
                                uint64_t durationMs);
