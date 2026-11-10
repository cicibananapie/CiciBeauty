// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <span>
#include <string_view>

namespace BackgroundAsset {

enum class Type {
    None,
    Image,
    Video,
    Unsupported,
};

Type typeFromPath(std::u8string_view path);
const char* typeName(Type type);
bool isImage(std::u8string_view path);
bool isVideo(std::u8string_view path);
bool isSupported(std::u8string_view path);

std::span<const std::u8string_view> imageExtensions();
std::span<const std::u8string_view> videoExtensions();

}  // namespace BackgroundAsset
