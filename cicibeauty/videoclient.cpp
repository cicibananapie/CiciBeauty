// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "videoclient.h"
#include <obs.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include "lumaclient.h"
#include "uuid.h"
#include "log.h"

using namespace cici::protocol;
namespace {
// Keep the wire enum independent; fail compilation if OBS changes its values.
#define CHECK_FORMAT(obs, wire)                                                                                        \
    static_assert(static_cast<uint32_t>(VIDEO_FORMAT_##obs) == static_cast<uint32_t>(FrameFormat::wire))
CHECK_FORMAT(I420, I420);
CHECK_FORMAT(NV12, NV12);
CHECK_FORMAT(YVYU, YVYU);
CHECK_FORMAT(YUY2, YUY2);
CHECK_FORMAT(UYVY, UYVY);
CHECK_FORMAT(RGBA, RGBA);
CHECK_FORMAT(BGRA, BGRA);
CHECK_FORMAT(BGRX, BGRX);
CHECK_FORMAT(Y800, Y800);
CHECK_FORMAT(I444, I444);
CHECK_FORMAT(BGR3, BGR3);
CHECK_FORMAT(I422, I422);
CHECK_FORMAT(I40A, I40A);
CHECK_FORMAT(I42A, I42A);
CHECK_FORMAT(YUVA, YUVA);
CHECK_FORMAT(AYUV, VUYA);
CHECK_FORMAT(I010, I010);
CHECK_FORMAT(P010, P010);
CHECK_FORMAT(I210, I210);
CHECK_FORMAT(I412, I412);
CHECK_FORMAT(YA2L, YA2L);
CHECK_FORMAT(P216, P216);
CHECK_FORMAT(P416, P416);
CHECK_FORMAT(V210, V210);
CHECK_FORMAT(R10L, R10L);
#undef CHECK_FORMAT

FrameColorSpace colorSpace(const obs_source_frame& f) {
    if (!format_is_yuv(f.format)) {
        return FrameColorSpace::SRGB;
    }
    float best = 1e30f;
    FrameColorSpace result = FrameColorSpace::BT709;
    for (const auto cs : {VIDEO_CS_601, VIDEO_CS_709, VIDEO_CS_2100_PQ}) {
        float m[16]{}, low[3]{}, high[3]{};
        if (!video_format_get_parameters_for_format(cs, f.full_range ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL, f.format,
                                                    m, low, high)) {
            continue;
        }
        float distance = 0;
        for (size_t i = 0; i < 16; ++i) {
            distance += std::fabs(m[i] - f.color_matrix[i]);
        }
        if (distance < best) {
            best = distance;
            result = cs == VIDEO_CS_601       ? FrameColorSpace::BT601
                     : cs == VIDEO_CS_709     ? FrameColorSpace::BT709
                     : f.trc == VIDEO_TRC_HLG ? FrameColorSpace::BT2100HLG
                                              : FrameColorSpace::BT2100PQ;
        }
    }
    // As before, absent color metadata defaults to 709.
    bool hasMatrix = false;
    for (float v : f.color_matrix) {
        hasMatrix |= std::fabs(v) > 1e-6f;
    }
    return hasMatrix ? result : FrameColorSpace::BT709;
}
bool validPlanes(const obs_source_frame& f, const FrameLayout& l) {
    for (uint32_t p = 0; p < l.count; ++p) {
        if (!f.data[p] || f.linesize[p] < l.planes[p].stride) {
            return false;
        }
    }
    return l.bytes != 0;
}
void copyToOBS(std::span<const uint8_t> bytes, obs_source_frame& f, const FrameLayout& l) {
    FrameHeader header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    for (uint32_t p = 0; p < l.count; ++p) {
        const auto& plane = l.planes[p];
        for (uint32_t y = 0; y < plane.rows; ++y) {
            std::memcpy(f.data[p] + size_t(y) * f.linesize[p],
                        bytes.data() + sizeof(header) + plane.offset + size_t(y) * plane.stride, plane.stride);
        }
    }
    // OBS owns the carrier and its timing; restore the metadata of its pixels.
    f.flip = header.sourceFlip != 0;
    f.flags = header.flags;
    f.max_luminance = header.maxLuminance;
    std::memcpy(f.color_matrix, header.colorMatrix, sizeof(header.colorMatrix));
    std::memcpy(f.color_range_min, header.rangeMin, sizeof(header.rangeMin));
    std::memcpy(f.color_range_max, header.rangeMax, sizeof(header.rangeMax));
}
}

VideoClient::~VideoClient() {
    stop();
}
std::unique_ptr<VideoClient::Pair> VideoClient::createPair(const FrameSpec& spec) {
    auto pair = std::make_unique<Pair>();
    auto name = CiciUUID::generate();
    name.erase(std::remove(name.begin(), name.end(), u8'-'), name.end());
    pair->channel = u8"v" + name.substr(0, 20);
    pair->spec = spec;
    const auto bytes = spec == FrameSpec{} ? kBootstrapSlotBytes : frameSlotBytes(frameLayout(spec));
    if (!bytes || !pair->input.create(pair->channel + u8"i", kFrameQueueCapacity, bytes) ||
        !pair->output.create(pair->channel + u8"o", kFrameQueueCapacity, bytes)) {
        LogError << "Create video channel failed" << logEnd;
        return {};
    }
    return pair;
}
void VideoClient::close() {
    if (current_ && !current_->output.readView().empty()) {
        current_->output.releaseRead();
    }
    current_.reset();
    retired_.clear();
    sequence_ = 0;
}
std::u8string VideoClient::start() {
    std::lock_guard lock(mutex_);
    close();
    current_ = createPair({});
    return current_ ? current_->channel : std::u8string{};
}
void VideoClient::stop() {
    std::lock_guard lock(mutex_);
    close();
}

void VideoClient::process(obs_source_frame& frame, uint32_t transform) {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || !current_) {
        return;
    }
    if (!LumaClient::instance().connectionId()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    FrameSpec spec{frame.width,
                   frame.height,
                   static_cast<FrameFormat>(frame.format),
                   colorSpace(frame),
                   frame.full_range ? 1U : 0U,
                   static_cast<uint32_t>(frame.trc)};
    const auto layout = frameLayout(spec);
    if (!validPlanes(frame, layout)) {
        return;
    }
    std::erase_if(retired_, [](const auto& pair) {
        return pair->input.isDrained();
    });
    if (current_->input.isClosed() || current_->output.isClosed()) {
        close();
        return;
    }
    if (spec != current_->spec) {
        std::span<uint8_t> record;
        if (current_->input.acquireWrite(record, true) != SHMFrame::Result::Success) {
            return;
        }
        auto next = createPair(spec);
        if (!next) {
            current_->input.cancelWrite();
            return;
        }
        FrameSwitch change;
        change.spec = spec;
        std::copy(next->channel.begin(), next->channel.end(), change.channel);
        std::memcpy(record.data(), &change, sizeof(change));
        if (current_->input.commitWrite(sizeof(change)) != SHMFrame::Result::Success) {
            return;
        }
        // This input receives no further writes. Do not close/unlink either old
        // endpoint until luma has released its final (switch) record.
        retired_.push_back(std::move(current_));
        current_ = std::move(next);
        LogInfo << "Switch video channel " << current_->channel << " size=" << spec.width << "x" << spec.height
                << " format=" << static_cast<uint32_t>(spec.format) << logEnd;
    }
    const uint32_t effectiveTransform = transform ^ (uint32_t(frame.flip) << 17);
    if (transform_ != effectiveTransform) {
        if (!current_->output.readView().empty()) {
            current_->output.releaseRead();
        }
        transform_ = effectiveTransform;
    }
    std::span<uint8_t> write;
    if (current_->input.acquireWrite(write) == SHMFrame::Result::Success) {
        FrameHeader h;
        h.spec = spec;
        h.sequence = ++sequence_;
        h.timestamp = frame.timestamp;
        h.submittedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
        h.rotation = transform & 0xffffU;
        h.flipHorizontal = (transform >> 16) & 1;
        h.flipVertical = ((transform >> 17) & 1) ^ frame.flip;
        h.flags = frame.flags;
        h.sourceFlip = frame.flip;
        h.maxLuminance = frame.max_luminance;
        std::memcpy(h.colorMatrix, frame.color_matrix, sizeof(h.colorMatrix));
        std::memcpy(h.rangeMin, frame.color_range_min, sizeof(h.rangeMin));
        std::memcpy(h.rangeMax, frame.color_range_max, sizeof(h.rangeMax));
        std::memcpy(write.data(), &h, sizeof(h));
        for (uint32_t p = 0; p < layout.count; ++p) {
            const auto& plane = layout.planes[p];
            for (uint32_t y = 0; y < plane.rows; ++y) {
                std::memcpy(write.data() + sizeof(h) + plane.offset + size_t(y) * plane.stride,
                            frame.data[p] + size_t(y) * frame.linesize[p], plane.stride);
            }
        }
        if (current_->input.commitWrite(frameSlotBytes(layout)) != SHMFrame::Result::Success) {
            LogError << "Submit video frame failed" << logEnd;
            close();
            return;
        }
    } // Full input queue: skip this submission; never overwrite a held slot.
    // Normally consume one FIFO result. Drain only results too old to display.
    auto& output = current_->output;
    for (uint32_t n = 0; n < kFrameQueueCapacity; ++n) {
        if (output.readableCount() == 0) {
            break;
        }

        if (output.readableCount() == 1) {
            if (!output.readView().empty()) { // the old frame is the only one can be use, so re-use it.
                break;
            }
        } else {
            if (!output.readView().empty() && output.releaseRead() != SHMFrame::Result::Success) {
                return;
            }
        }

        if (output.acquireRead() != SHMFrame::Result::Success) {
            return;
        }
        FrameHeader h;
        const bool valid = readFrame(output.readView(), current_->spec, h) && h.rotation == (transform & 0xffffU) &&
                           h.flipHorizontal == ((transform >> 16) & 1) &&
                           h.flipVertical == (((transform >> 17) & 1) ^ frame.flip);
        const auto age = std::chrono::nanoseconds(
            h.submittedNs
                ? std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count() - h.submittedNs
                : INT64_MAX);
        if (!valid) {
            output.releaseRead();
        }
        if (valid && age < std::chrono::milliseconds(100)) {
            break;
        }
    }
    if (!output.readView().empty()) {
        copyToOBS(output.readView(), frame, layout);
    }
}
