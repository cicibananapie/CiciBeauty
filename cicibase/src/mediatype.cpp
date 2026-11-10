// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include "mediatype.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>

namespace {

constexpr std::array<std::u8string_view, 5> kImageExtensions = {
    u8".png", u8".jpg", u8".jpeg", u8".bmp", u8".gif",
};

constexpr std::array<std::u8string_view, 4> kVideoExtensions = {
    u8".mp4", u8".mov", u8".webm", u8".mkv",
};

std::u8string lowerAscii(std::u8string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char8_t c) {
        return static_cast<char8_t>(std::tolower(static_cast<unsigned char>(c)));
    });
    return value;
}

template<size_t N>
bool contains(const std::array<std::u8string_view, N>& extensions,
              std::u8string_view extension) {
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

}  // namespace

namespace BackgroundAsset {

Type typeFromPath(std::u8string_view path) {
    if (path.empty()) {
        return Type::None;
    }

    const auto extension = lowerAscii(
        std::filesystem::path(std::u8string(path)).extension().u8string());
    if (contains(kImageExtensions, extension)) {
        return Type::Image;
    }
    if (contains(kVideoExtensions, extension)) {
        return Type::Video;
    }
    return Type::Unsupported;
}

const char* typeName(Type type) {
    switch (type) {
        case Type::None: return "none";
        case Type::Image: return "image";
        case Type::Video: return "video";
        case Type::Unsupported: return "unknown";
    }
    return "unknown";
}

bool isImage(std::u8string_view path) {
    return typeFromPath(path) == Type::Image;
}

bool isVideo(std::u8string_view path) {
    return typeFromPath(path) == Type::Video;
}

bool isSupported(std::u8string_view path) {
    const Type type = typeFromPath(path);
    return type == Type::Image || type == Type::Video;
}

std::span<const std::u8string_view> imageExtensions() {
    return kImageExtensions;
}

std::span<const std::u8string_view> videoExtensions() {
    return kVideoExtensions;
}

}  // namespace BackgroundAsset
