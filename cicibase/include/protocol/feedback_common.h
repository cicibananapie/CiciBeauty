// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "binarymsg.h"

enum class FeedbackFinishStatus : uint32_t {
    Succeeded,
    Failed,
    Cancelled,
};

constexpr uint32_t kMsgSubmitFeedback = 1200;
constexpr uint32_t kMsgCancelFeedback = 1201;
constexpr uint32_t kMsgFeedbackFinished = 1300;
constexpr uint32_t kMsgFeedbackFinishedAck = 1301;

struct SubmitFeedbackMsg : public IPCMessage {
    std::u8string description_;
    std::u8string email_;
    std::vector<std::u8string> screenshotPaths_;
    std::u8string deviceInfo_;
    bool includeLogs_ = false;

    uint32_t id() const override { return kMsgSubmitFeedback; }
    uint32_t pack(BinaryPack& bin) const override {
        uint32_t size = bin.push(description_);
        size += bin.push(email_);
        size += bin.push(screenshotPaths_);
        size += bin.push(deviceInfo_);
        size += bin.push(includeLogs_);
        return size;
    }
    bool unpack(BinaryUnpack& bin) override {
        return bin.pop(description_) && bin.pop(email_) && bin.pop(screenshotPaths_) &&
               bin.pop(deviceInfo_) && bin.pop(includeLogs_);
    }
};

struct CancelFeedbackMsg : public IPCMessage {
    uint32_t id() const override { return kMsgCancelFeedback; }
    uint32_t pack(BinaryPack&) const override { return 0; }
    bool unpack(BinaryUnpack&) override { return true; }
};

struct FeedbackFinishedMsg : public IPCMessage {
    FeedbackFinishStatus status_ = FeedbackFinishStatus::Failed;
    std::u8string feedbackId_;
    std::u8string errorCode_;
    std::u8string message_;

    uint32_t id() const override { return kMsgFeedbackFinished; }
    uint32_t pack(BinaryPack& bin) const override {
        uint32_t size = bin.push(static_cast<uint32_t>(status_));
        size += bin.push(feedbackId_);
        size += bin.push(errorCode_);
        size += bin.push(message_);
        return size;
    }
    bool unpack(BinaryUnpack& bin) override {
        uint32_t status = 0;
        if (!bin.pop(status) || status > static_cast<uint32_t>(FeedbackFinishStatus::Cancelled)) {
            return false;
        }
        status_ = static_cast<FeedbackFinishStatus>(status);
        return bin.pop(feedbackId_) && bin.pop(errorCode_) && bin.pop(message_);
    }
};

struct FeedbackFinishedAckMsg : public IPCMessage {
    uint32_t id() const override { return kMsgFeedbackFinishedAck; }
    uint32_t pack(BinaryPack&) const override { return 0; }
    bool unpack(BinaryUnpack&) override { return true; }
};
