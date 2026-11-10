// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

namespace cici::protocol {

// CPU transport ABI, independent of OBS and MRE. Never put native pointers here.
enum class FrameFormat : uint32_t {
    None,
    I420,
    NV12,
    YVYU,
    YUY2,
    UYVY,
    RGBA,
    BGRA,
    BGRX,
    Y800,
    I444,
    BGR3,
    I422,
    I40A,
    I42A,
    YUVA,
    VUYA,
    I010,
    P010,
    I210,
    I412,
    YA2L,
    P216,
    P416,
    V210,
    R10L
};
enum class FrameColorSpace : uint32_t {
    SRGB,
    BT601,
    BT709,
    BT2100PQ,
    BT2100HLG
};

struct FrameSpec {
    uint32_t width = 0, height = 0;
    FrameFormat format = FrameFormat::None;
    FrameColorSpace colorSpace = FrameColorSpace::BT709;
    uint32_t fullRange = 0, trc = 0;
    bool operator==(const FrameSpec&) const = default;
};
struct FramePlane {
    uint32_t offset = 0, stride = 0, rows = 0;
};
struct FrameLayout {
    std::array<FramePlane, 4> planes{};
    uint32_t count = 0, bytes = 0;
};

// Tightly packed planes make AI input contiguous as well as directly uploadable.
inline FrameLayout frameLayout(const FrameSpec& s) {
    FrameLayout l;
    if (!s.width || !s.height || s.width > 16384 || s.height > 16384 || static_cast<uint32_t>(s.colorSpace) > 4 ||
        s.trc > 3 || s.fullRange > 1) {
        return l;
    }
    const uint32_t w = s.width, h = s.height, cw = (w + 1) / 2, ch = (h + 1) / 2;
    auto plane = [&](uint32_t stride, uint32_t rows) {
        l.planes[l.count++] = {l.bytes, stride, rows};
        l.bytes += stride * rows;
    };
    switch (s.format) {
    case FrameFormat::I420:
    case FrameFormat::I40A:
        plane(w, h);
        plane(cw, ch);
        plane(cw, ch);
        if (s.format == FrameFormat::I40A) {
            plane(w, h);
        }
        break;
    case FrameFormat::I422:
    case FrameFormat::I42A:
        plane(w, h);
        plane(cw, h);
        plane(cw, h);
        if (s.format == FrameFormat::I42A) {
            plane(w, h);
        }
        break;
    case FrameFormat::NV12:
        plane(w, h);
        plane(cw * 2, ch);
        break;
    case FrameFormat::I444:
    case FrameFormat::YUVA:
        plane(w, h);
        plane(w, h);
        plane(w, h);
        if (s.format == FrameFormat::YUVA) {
            plane(w, h);
        }
        break;
    case FrameFormat::I010:
    case FrameFormat::I210:
        plane(w * 2, h);
        plane(cw * 2, s.format == FrameFormat::I010 ? ch : h);
        plane(cw * 2, s.format == FrameFormat::I010 ? ch : h);
        break;
    case FrameFormat::P010:
    case FrameFormat::P216:
        plane(w * 2, h);
        plane(cw * 4, s.format == FrameFormat::P010 ? ch : h);
        break;
    case FrameFormat::I412:
    case FrameFormat::YA2L:
        plane(w * 2, h);
        plane(w * 2, h);
        plane(w * 2, h);
        if (s.format == FrameFormat::YA2L) {
            plane(w * 2, h);
        }
        break;
    case FrameFormat::P416:
        plane(w * 2, h);
        plane(w * 4, h);
        break;
    case FrameFormat::YVYU:
    case FrameFormat::YUY2:
    case FrameFormat::UYVY:
        plane(cw * 4, h);
        break;
    case FrameFormat::RGBA:
    case FrameFormat::BGRA:
    case FrameFormat::BGRX:
    case FrameFormat::VUYA:
    case FrameFormat::R10L:
        plane(w * 4, h);
        break;
    case FrameFormat::Y800:
        plane(w, h);
        break;
    case FrameFormat::BGR3:
        plane(w * 3, h);
        break;
    case FrameFormat::V210:
        plane(((w + 5) / 6) * 16, h);
        break;
    default:
        return {};
    }
    // Bound a single frame, including malicious channel specifications.
    if (l.bytes > 256U * 1024 * 1024) {
        return {};
    }
    return l;
}

struct FrameHeader {
    uint32_t version = 1;
    FrameSpec spec;
    uint64_t sequence = 0, timestamp = 0, submittedNs = 0;
    // Rotation in degrees; flips apply after rotation. Independent of MRE flags.
    uint32_t rotation = 0, flipHorizontal = 0, flipVertical = 0;
    uint32_t flags = 0, maxLuminance = 0, sourceFlip = 0;
    float colorMatrix[16] = {}, rangeMin[3] = {}, rangeMax[3] = {};
};
static_assert(std::is_trivially_copyable_v<FrameHeader>);
static_assert(sizeof(FrameHeader) == 168);
// A control-only input record. Published as the LAST record in the old input.
// Copy/open the new endpoints and finish all old output writes before releasing
// it: the producer uses the old input's drained cursor to reclaim both queues.
struct FrameSwitch {
    uint32_t version = 2;
    FrameSpec spec;
    char8_t channel[24] = {};
};
static_assert(std::is_trivially_copyable_v<FrameSwitch>);
static_assert(sizeof(FrameSwitch) <= sizeof(FrameHeader));
inline constexpr uint32_t kBootstrapSlotBytes = sizeof(FrameHeader);
inline bool validVideoChannel(std::u8string_view channel) {
    if (channel.empty() || channel.size() >= sizeof(FrameSwitch::channel)) {
        return false;
    }
    for (auto c : channel) {
        if (!((c >= u8'a' && c <= u8'z') || (c >= u8'A' && c <= u8'Z') || (c >= u8'0' && c <= u8'9') || c == u8'_' ||
              c == u8'-')) {
            return false;
        }
    }
    return true;
}
inline bool readFrameSwitch(std::span<const uint8_t> bytes, FrameSwitch& message) {
    if (bytes.size() != sizeof(message)) {
        return false;
    }
    std::memcpy(&message, bytes.data(), sizeof(message));
    const auto end = static_cast<const char8_t*>(std::memchr(message.channel, 0, sizeof(message.channel)));
    return message.version == 2 && end && frameLayout(message.spec).bytes &&
           validVideoChannel({message.channel, static_cast<size_t>(end - message.channel)});
}
inline constexpr uint32_t kFrameQueueCapacity = 3;
inline constexpr uint32_t frameSlotBytes(const FrameLayout& l) {
    return l.bytes ? sizeof(FrameHeader) + l.bytes : 0;
}
inline bool readFrame(std::span<const uint8_t> bytes, const FrameSpec& expected, FrameHeader& header) {
    if (bytes.size() < sizeof(header)) {
        return false;
    }
    std::memcpy(&header, bytes.data(), sizeof(header));
    return header.version == 1 && header.spec == expected && header.sequence != 0 &&
           (header.rotation == 0 || header.rotation == 90 || header.rotation == 180 || header.rotation == 270) &&
           header.flipHorizontal <= 1 && header.flipVertical <= 1 && header.sourceFlip <= 1 &&
           bytes.size() == frameSlotBytes(frameLayout(expected));
}

} // namespace cici::protocol
