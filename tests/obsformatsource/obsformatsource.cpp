// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#if defined(__clang__) && defined(_M_X64)
#include <cstdint>

static inline unsigned __int64 ciciTestUnsignedDivide128(
    unsigned __int64 high,
    unsigned __int64 low,
    unsigned __int64 divisor,
    unsigned __int64* remainder)
{
    const unsigned __int128 dividend =
        (static_cast<unsigned __int128>(high) << 64) | low;
    *remainder = static_cast<unsigned __int64>(dividend % divisor);
    return static_cast<unsigned __int64>(dividend / divisor);
}

#define _udiv128 ciciTestUnsignedDivide128
#endif

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <media-io/video-scaler.h>
#include <util/platform.h>

#if defined(__clang__) && defined(_M_X64)
#undef _udiv128
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Cici Beauty")

namespace {

constexpr uint32_t kDefaultWidth = 1280;
constexpr uint32_t kDefaultHeight = 720;
constexpr uint32_t kDefaultFps = 30;
constexpr uint32_t kMinimumDimension = 16;
constexpr uint32_t kMaximumWidth = 8192;
constexpr uint32_t kMaximumHeight = 4320;
constexpr uint32_t kMaximumFps = 240;
constexpr auto kControlPollInterval = std::chrono::milliseconds(100);

constexpr char kFormatSetting[] = "format";
constexpr char kPaddingSetting[] = "padding";
constexpr char kFullRangeSetting[] = "full_range";
constexpr char kFlipSetting[] = "flip";
constexpr char kWidthSetting[] = "width";
constexpr char kHeightSetting[] = "height";
constexpr char kFpsSetting[] = "fps";
constexpr char kFramePathSetting[] = "frame_path";

constexpr char kControlPathEnvironment[] =
    "CICI_OBS_FORMAT_TEST_CONTROL";
constexpr char kStatusPathEnvironment[] = "CICI_OBS_FORMAT_TEST_STATUS";
constexpr char kFramePathEnvironment[] = "CICI_OBS_FORMAT_TEST_FRAME";
constexpr char kBlankFramePathEnvironment[] =
    "CICI_OBS_FORMAT_TEST_BLANK_FRAME";
constexpr char kCaptureDirectoryEnvironment[] =
    "CICI_OBS_FORMAT_TEST_CAPTURE_DIR";
constexpr char kCaptureMainEnvironment[] =
    "CICI_OBS_FORMAT_TEST_CAPTURE_MAIN";
constexpr char kFilterControlPathEnvironment[] =
    "CICI_OBS_FILTER_TEST_CONTROL";
constexpr char kFilterStatusPathEnvironment[] =
    "CICI_OBS_FILTER_TEST_STATUS";

std::atomic_bool gFilterControlStop{false};
std::thread gFilterControlThread;

struct PendingCapture {
    std::string outputPath;
    uint64_t sequence = 0;
    std::string format;
};

std::mutex gCaptureMutex;
PendingCapture gPendingCapture;

struct FormatEntry {
    const char* name;
    video_format format;
};

constexpr std::array<FormatEntry, 25> kFormats{{
    {"I420", VIDEO_FORMAT_I420},
    {"NV12", VIDEO_FORMAT_NV12},
    {"YVYU", VIDEO_FORMAT_YVYU},
    {"YUY2", VIDEO_FORMAT_YUY2},
    {"UYVY", VIDEO_FORMAT_UYVY},
    {"RGBA", VIDEO_FORMAT_RGBA},
    {"BGRA", VIDEO_FORMAT_BGRA},
    {"BGRX", VIDEO_FORMAT_BGRX},
    {"Y800", VIDEO_FORMAT_Y800},
    {"I444", VIDEO_FORMAT_I444},
    {"BGR3", VIDEO_FORMAT_BGR3},
    {"I422", VIDEO_FORMAT_I422},
    {"I40A", VIDEO_FORMAT_I40A},
    {"I42A", VIDEO_FORMAT_I42A},
    {"YUVA", VIDEO_FORMAT_YUVA},
    {"AYUV", VIDEO_FORMAT_AYUV},
    {"I010", VIDEO_FORMAT_I010},
    {"P010", VIDEO_FORMAT_P010},
    {"I210", VIDEO_FORMAT_I210},
    {"I412", VIDEO_FORMAT_I412},
    {"YA2L", VIDEO_FORMAT_YA2L},
    {"P216", VIDEO_FORMAT_P216},
    {"P416", VIDEO_FORMAT_P416},
    {"V210", VIDEO_FORMAT_V210},
    {"R10L", VIDEO_FORMAT_R10L},
}};

struct SourceConfiguration {
    uint64_t sequence = 0;
    video_format format = VIDEO_FORMAT_NV12;
    uint32_t padding = 0;
    bool fullRange = false;
    bool flip = false;
    uint32_t width = kDefaultWidth;
    uint32_t height = kDefaultHeight;
    uint32_t fps = kDefaultFps;
    std::string framePath;
};

struct PlaneLayout {
    std::array<uint32_t, MAX_AV_PLANES> rowBytes{};
    std::array<uint32_t, MAX_AV_PLANES> heights{};
};

struct PreparedFrame {
    obs_source_frame frame{};
    std::vector<uint8_t> storage;
    std::string inputKind;
};

struct BgraClipReader {
    std::ifstream input;
    size_t frameSize = 0;
    uint64_t frameCount = 0;
    uint64_t nextFrame = 0;

    bool open(const std::string& path, uint32_t width, uint32_t height)
    {
        input.close();
        input.clear();
        frameCount = 0;
        nextFrame = 0;
        frameSize = static_cast<size_t>(width) * height * 4;
        if (path.empty()) {
            return false;
        }

        input.open(path, std::ios::binary | std::ios::ate);
        if (!input) {
            return false;
        }
        const std::streamoff size = input.tellg();
        if (size <= 0 || frameSize == 0 ||
            size % static_cast<std::streamoff>(frameSize) != 0) {
            input.close();
            return false;
        }

        frameCount = static_cast<uint64_t>(
            size / static_cast<std::streamoff>(frameSize));
        input.seekg(0);
        return input.good();
    }

    bool readNext(std::vector<uint8_t>& bgra)
    {
        if (!input || frameCount == 0) {
            return false;
        }
        if (nextFrame == frameCount) {
            input.clear();
            input.seekg(0);
            nextFrame = 0;
        }

        bgra.resize(frameSize);
        input.read(reinterpret_cast<char*>(bgra.data()),
                   static_cast<std::streamsize>(frameSize));
        if (input.gcount() !=
            static_cast<std::streamsize>(frameSize)) {
            return false;
        }
        ++nextFrame;
        return true;
    }
};

struct FormatTestSource {
    obs_source_t* source = nullptr;
    std::atomic_bool stop = false;
    std::thread outputThread;
    std::mutex settingsMutex;
    SourceConfiguration settings;
    uint64_t settingsRevision = 0;
};

[[maybe_unused]] uint8_t clampByte(double value)
{
    return static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

uint16_t clamp10(double value)
{
    return static_cast<uint16_t>(
        std::clamp(std::lround(value), 0L, 1023L));
}

uint16_t clamp16(double value)
{
    return static_cast<uint16_t>(
        std::clamp(std::lround(value), 0L, 65535L));
}

char upperAscii(char value)
{
    if (value >= 'a' && value <= 'z') {
        return static_cast<char>(value - 'a' + 'A');
    }
    return value;
}

bool equalAsciiCaseInsensitive(const std::string& left, const char* right)
{
    const size_t rightLength = std::strlen(right);
    if (left.size() != rightLength) {
        return false;
    }

    for (size_t i = 0; i < left.size(); ++i) {
        if (upperAscii(left[i]) != upperAscii(right[i])) {
            return false;
        }
    }
    return true;
}

const FormatEntry* findFormat(const std::string& name)
{
    const auto iterator =
        std::find_if(kFormats.begin(), kFormats.end(), [&](const auto& item) {
            return equalAsciiCaseInsensitive(name, item.name);
        });
    return iterator == kFormats.end() ? nullptr : &*iterator;
}

const char* formatName(video_format format)
{
    const auto iterator =
        std::find_if(kFormats.begin(), kFormats.end(), [&](const auto& item) {
            return item.format == format;
        });
    return iterator == kFormats.end() ? "UNKNOWN" : iterator->name;
}

std::string environmentValue(const char* name)
{
#ifdef _WIN32
    char* value = nullptr;
    size_t valueLength = 0;
    if (_dupenv_s(&value, &valueLength, name) != 0 || !value) {
        return {};
    }
    const std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

void onFrontendEvent(enum obs_frontend_event event, void*)
{
    if (event != OBS_FRONTEND_EVENT_SCREENSHOT_TAKEN) {
        return;
    }

    char* screenshotPath = obs_frontend_get_last_screenshot();
    PendingCapture capture;
    {
        std::lock_guard lock(gCaptureMutex);
        capture = std::move(gPendingCapture);
        gPendingCapture = {};
    }
    if (!screenshotPath || capture.outputPath.empty()) {
        bfree(screenshotPath);
        return;
    }

    std::error_code error;
    const std::filesystem::path destination =
        capture.outputPath +
        std::filesystem::path(screenshotPath).extension().string();
    std::filesystem::copy_file(
        screenshotPath, destination,
        std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        blog(LOG_ERROR,
             "[ciciobsformattest] screenshot copy failed sequence=%llu "
             "format=%s error=%s",
             static_cast<unsigned long long>(capture.sequence),
             capture.format.c_str(), error.message().c_str());
    } else {
        blog(LOG_INFO,
             "[ciciobsformattest] screenshot saved sequence=%llu format=%s "
             "path=%s",
             static_cast<unsigned long long>(capture.sequence),
             capture.format.c_str(), destination.string().c_str());
    }
    bfree(screenshotPath);
}

bool requestScreenshot(obs_source_t* source,
                       const SourceConfiguration& configuration)
{
    const std::string directory =
        environmentValue(kCaptureDirectoryEnvironment);
    if (directory.empty()) {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }

    char filename[64]{};
    std::snprintf(filename, sizeof(filename), "%02llu-%s",
                  static_cast<unsigned long long>(configuration.sequence),
                  formatName(configuration.format));
    {
        std::lock_guard lock(gCaptureMutex);
        if (!gPendingCapture.outputPath.empty()) {
            return false;
        }
        gPendingCapture.outputPath =
            (std::filesystem::path(directory) / filename).string();
        gPendingCapture.sequence = configuration.sequence;
        gPendingCapture.format = formatName(configuration.format);
    }
    if (environmentValue(kCaptureMainEnvironment) == "1") {
        obs_frontend_take_screenshot();
    } else {
        obs_frontend_take_source_screenshot(source);
    }
    return true;
}

PlaneLayout planeLayout(video_format format, uint32_t width, uint32_t height)
{
    PlaneLayout layout;
    const uint32_t halfWidth = (width + 1) / 2;
    const uint32_t halfHeight = (height + 1) / 2;

    switch (format) {
    case VIDEO_FORMAT_BGR3:
        layout.rowBytes[0] = width * 3;
        layout.heights[0] = height;
        break;
    case VIDEO_FORMAT_RGBA:
    case VIDEO_FORMAT_BGRA:
    case VIDEO_FORMAT_BGRX:
    case VIDEO_FORMAT_AYUV:
    case VIDEO_FORMAT_R10L:
        layout.rowBytes[0] = width * 4;
        layout.heights[0] = height;
        break;
    case VIDEO_FORMAT_YVYU:
    case VIDEO_FORMAT_YUY2:
    case VIDEO_FORMAT_UYVY:
        layout.rowBytes[0] = width * 2;
        layout.heights[0] = height;
        break;
    case VIDEO_FORMAT_Y800:
        layout.rowBytes[0] = width;
        layout.heights[0] = height;
        break;
    case VIDEO_FORMAT_I420:
        layout.rowBytes = {width, halfWidth, halfWidth};
        layout.heights = {height, halfHeight, halfHeight};
        break;
    case VIDEO_FORMAT_NV12:
        layout.rowBytes = {width, width};
        layout.heights = {height, halfHeight};
        break;
    case VIDEO_FORMAT_I444:
        layout.rowBytes = {width, width, width};
        layout.heights = {height, height, height};
        break;
    case VIDEO_FORMAT_I422:
        layout.rowBytes = {width, halfWidth, halfWidth};
        layout.heights = {height, height, height};
        break;
    case VIDEO_FORMAT_I40A:
        layout.rowBytes = {width, halfWidth, halfWidth, width};
        layout.heights = {height, halfHeight, halfHeight, height};
        break;
    case VIDEO_FORMAT_I42A:
        layout.rowBytes = {width, halfWidth, halfWidth, width};
        layout.heights = {height, height, height, height};
        break;
    case VIDEO_FORMAT_YUVA:
        layout.rowBytes = {width, width, width, width};
        layout.heights = {height, height, height, height};
        break;
    case VIDEO_FORMAT_I010:
        layout.rowBytes = {width * 2, width, width};
        layout.heights = {height, halfHeight, halfHeight};
        break;
    case VIDEO_FORMAT_P010:
        layout.rowBytes = {width * 2, width * 2};
        layout.heights = {height, halfHeight};
        break;
    case VIDEO_FORMAT_I210:
        layout.rowBytes = {width * 2, width, width};
        layout.heights = {height, height, height};
        break;
    case VIDEO_FORMAT_I412:
        layout.rowBytes = {width * 2, width * 2, width * 2};
        layout.heights = {height, height, height};
        break;
    case VIDEO_FORMAT_YA2L:
        layout.rowBytes = {width * 2, width * 2, width * 2, width * 2};
        layout.heights = {height, height, height, height};
        break;
    case VIDEO_FORMAT_P216:
        layout.rowBytes = {width * 2, width * 2};
        layout.heights = {height, height};
        break;
    case VIDEO_FORMAT_P416:
        layout.rowBytes = {width * 2, width * 4};
        layout.heights = {height, height};
        break;
    case VIDEO_FORMAT_V210: {
        layout.rowBytes[0] = ((width + 5) / 6) * 16;
        layout.heights[0] = height;
        break;
    }
    case VIDEO_FORMAT_NONE:
        break;
    }

    return layout;
}

bool supportsInputGeometry(video_format format, uint32_t width,
                           uint32_t height)
{
    switch (format) {
    case VIDEO_FORMAT_I420:
    case VIDEO_FORMAT_NV12:
    case VIDEO_FORMAT_I40A:
    case VIDEO_FORMAT_I010:
    case VIDEO_FORMAT_P010:
        return (width % 2) == 0 && (height % 2) == 0;
    case VIDEO_FORMAT_YVYU:
    case VIDEO_FORMAT_YUY2:
    case VIDEO_FORMAT_UYVY:
    case VIDEO_FORMAT_I422:
    case VIDEO_FORMAT_I42A:
    case VIDEO_FORMAT_I210:
    case VIDEO_FORMAT_P216:
    case VIDEO_FORMAT_V210:
        return (width % 2) == 0;
    default:
        return true;
    }
}

bool allocateFrameStorage(PreparedFrame& output, video_format format,
                          uint32_t width, uint32_t height,
                          uint32_t padding)
{
    const PlaneLayout layout = planeLayout(format, width, height);
    size_t totalSize = 0;

    for (size_t plane = 0; plane < MAX_AV_PLANES; ++plane) {
        if (layout.rowBytes[plane] == 0 || layout.heights[plane] == 0) {
            continue;
        }
        const uint32_t lineSize = layout.rowBytes[plane] + padding;
        if (lineSize < layout.rowBytes[plane] ||
            totalSize > std::numeric_limits<size_t>::max() -
                            static_cast<size_t>(lineSize) *
                                layout.heights[plane]) {
            return false;
        }
        totalSize += static_cast<size_t>(lineSize) * layout.heights[plane];
    }

    if (totalSize == 0) {
        return false;
    }

    output.storage.assign(totalSize, 0);
    size_t offset = 0;
    for (size_t plane = 0; plane < MAX_AV_PLANES; ++plane) {
        if (layout.rowBytes[plane] == 0 || layout.heights[plane] == 0) {
            continue;
        }
        output.frame.data[plane] = output.storage.data() + offset;
        output.frame.linesize[plane] = layout.rowBytes[plane] + padding;
        offset += static_cast<size_t>(output.frame.linesize[plane]) *
                  layout.heights[plane];
    }
    return true;
}

void setPixel(std::vector<uint8_t>& bgra, uint32_t width, uint32_t x,
              uint32_t y, uint8_t red, uint8_t green, uint8_t blue)
{
    const size_t index = (static_cast<size_t>(y) * width + x) * 4;
    bgra[index] = blue;
    bgra[index + 1] = green;
    bgra[index + 2] = red;
    bgra[index + 3] = 255;
}

std::vector<uint8_t> makeFallbackBgra(uint32_t width, uint32_t height)
{
    std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t red = static_cast<uint8_t>(32 + x * 160 / width);
            const uint8_t green =
                static_cast<uint8_t>(32 + y * 160 / height);
            const uint8_t blue = static_cast<uint8_t>(48 + (x + y) % 96);
            setPixel(bgra, width, x, y, red, green, blue);
        }
    }

    const uint32_t markerSize = std::min({96U, width / 4, height / 4});
    for (uint32_t y = 0; y < markerSize; ++y) {
        for (uint32_t x = 0; x < markerSize; ++x) {
            setPixel(bgra, width, x, y, 235, 50, 50);
            setPixel(bgra, width, width - markerSize + x, y, 50, 210, 70);
            setPixel(bgra, width, x, height - markerSize + y, 50, 90, 235);
            setPixel(bgra, width, width - markerSize + x,
                     height - markerSize + y, 240, 210, 45);
        }
    }

    const int centerX = static_cast<int>(width / 2);
    const int centerY = static_cast<int>(height / 2);
    const double radiusX = std::max(1.0, width * 145.0 / 1280.0);
    const double radiusY = std::max(1.0, height * 205.0 / 720.0);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const double dx = (static_cast<int>(x) - centerX) / radiusX;
            const double dy = (static_cast<int>(y) - centerY) / radiusY;
            if (dx * dx + dy * dy <= 1.0) {
                setPixel(bgra, width, x, y, 224, 177, 142);
            }
        }
    }

    return bgra;
}

void rgbToYuv(uint8_t red, uint8_t green, uint8_t blue, bool fullRange,
              uint16_t& y, uint16_t& u, uint16_t& v)
{
    double yValue = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
    double uValue = -0.114572 * red - 0.385428 * green + 0.5 * blue + 128.0;
    double vValue = 0.5 * red - 0.454153 * green - 0.045847 * blue + 128.0;
    if (!fullRange) {
        yValue = 16.0 + yValue * 219.0 / 255.0;
        uValue = 128.0 + (uValue - 128.0) * 224.0 / 255.0;
        vValue = 128.0 + (vValue - 128.0) * 224.0 / 255.0;
    }
    y = clamp10(yValue * 4.0);
    u = clamp10(uValue * 4.0);
    v = clamp10(vValue * 4.0);
}

void rgbToYuv16(uint8_t red, uint8_t green, uint8_t blue, bool fullRange,
                uint16_t& y, uint16_t& u, uint16_t& v)
{
    double yValue = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
    double uValue = -0.114572 * red - 0.385428 * green + 0.5 * blue + 128.0;
    double vValue = 0.5 * red - 0.454153 * green - 0.045847 * blue + 128.0;
    if (!fullRange) {
        yValue = 16.0 + yValue * 219.0 / 255.0;
        uValue = 128.0 + (uValue - 128.0) * 224.0 / 255.0;
        vValue = 128.0 + (vValue - 128.0) * 224.0 / 255.0;
    }
    y = clamp16(yValue * 257.0);
    u = clamp16(uValue * 257.0);
    v = clamp16(vValue * 257.0);
}

void writeUint16(uint8_t* destination, uint16_t value)
{
    std::memcpy(destination, &value, sizeof(value));
}

void fillP16(PreparedFrame& output, const std::vector<uint8_t>& bgra,
             uint32_t width, uint32_t height, bool fullRange,
             bool subsampleChroma)
{
    for (uint32_t row = 0; row < height; ++row) {
        uint8_t* yDestination = output.frame.data[0] +
                                static_cast<size_t>(row) *
                                    output.frame.linesize[0];
        uint8_t* uvDestination = output.frame.data[1] +
                                 static_cast<size_t>(row) *
                                     output.frame.linesize[1];

        const uint32_t columnStep = subsampleChroma ? 2 : 1;
        for (uint32_t column = 0; column < width; column += columnStep) {
            const size_t firstSourceIndex =
                (static_cast<size_t>(row) * width + column) * 4;
            uint16_t firstY = 0;
            uint16_t firstU = 0;
            uint16_t firstV = 0;
            rgbToYuv16(bgra[firstSourceIndex + 2],
                       bgra[firstSourceIndex + 1], bgra[firstSourceIndex],
                       fullRange, firstY, firstU, firstV);
            writeUint16(yDestination + static_cast<size_t>(column) * 2,
                        firstY);

            uint32_t u = firstU;
            uint32_t v = firstV;
            if (subsampleChroma) {
                const size_t secondSourceIndex = firstSourceIndex + 4;
                uint16_t secondY = 0;
                uint16_t secondU = 0;
                uint16_t secondV = 0;
                rgbToYuv16(bgra[secondSourceIndex + 2],
                           bgra[secondSourceIndex + 1],
                           bgra[secondSourceIndex], fullRange, secondY,
                           secondU, secondV);
                writeUint16(
                    yDestination + static_cast<size_t>(column + 1) * 2,
                    secondY);
                u = (u + secondU) / 2;
                v = (v + secondV) / 2;
            }

            const size_t chroma = column / columnStep;
            const size_t destinationIndex = chroma * 4;
            writeUint16(uvDestination + destinationIndex,
                        static_cast<uint16_t>(u));
            writeUint16(uvDestination + destinationIndex + 2,
                        static_cast<uint16_t>(v));
        }
    }
}

void fillAyuv(PreparedFrame& output, const std::vector<uint8_t>& bgra,
              uint32_t width, uint32_t height, bool fullRange)
{
    for (uint32_t row = 0; row < height; ++row) {
        uint8_t* destination = output.frame.data[0] +
                               static_cast<size_t>(row) *
                                   output.frame.linesize[0];
        for (uint32_t column = 0; column < width; ++column) {
            const size_t sourceIndex =
                (static_cast<size_t>(row) * width + column) * 4;
            uint16_t y = 0;
            uint16_t u = 0;
            uint16_t v = 0;
            rgbToYuv(bgra[sourceIndex + 2], bgra[sourceIndex + 1],
                     bgra[sourceIndex], fullRange, y, u, v);
            destination[column * 4] = static_cast<uint8_t>(v / 4);
            destination[column * 4 + 1] = static_cast<uint8_t>(u / 4);
            destination[column * 4 + 2] = static_cast<uint8_t>(y / 4);
            destination[column * 4 + 3] = bgra[sourceIndex + 3];
        }
    }
}

void fillR10l(PreparedFrame& output, const std::vector<uint8_t>& bgra,
              uint32_t width, uint32_t height, bool fullRange)
{
    for (uint32_t row = 0; row < height; ++row) {
        uint8_t* destination = output.frame.data[0] +
                               static_cast<size_t>(row) *
                                   output.frame.linesize[0];
        for (uint32_t column = 0; column < width; ++column) {
            const size_t sourceIndex =
                (static_cast<size_t>(row) * width + column) * 4;
            const auto convert = [fullRange](uint8_t value) {
                return fullRange
                           ? clamp10(static_cast<double>(value) * 1023.0 / 255.0)
                           : clamp10(64.0 + static_cast<double>(value) *
                                                876.0 / 255.0);
            };
            const uint32_t red = convert(bgra[sourceIndex + 2]);
            const uint32_t green = convert(bgra[sourceIndex + 1]);
            const uint32_t blue = convert(bgra[sourceIndex]);
            const uint32_t packed =
                (red << 22) | (green << 12) | (blue << 2);
            std::memcpy(destination + column * 4, &packed, sizeof(packed));
        }
    }
}

void fillV210(PreparedFrame& output, const std::vector<uint8_t>& bgra,
              uint32_t width, uint32_t height, bool fullRange)
{
    for (uint32_t row = 0; row < height; ++row) {
        uint8_t* destinationBytes = output.frame.data[0] +
                                    static_cast<size_t>(row) *
                                        output.frame.linesize[0];
        auto* destination = reinterpret_cast<uint32_t*>(destinationBytes);
        size_t word = 0;
        for (uint32_t column = 0; column < width; column += 6) {
            std::array<uint16_t, 6> y{};
            std::array<uint16_t, 3> u{};
            std::array<uint16_t, 3> v{};
            for (uint32_t pixel = 0; pixel < 6; ++pixel) {
                const uint32_t x = std::min(column + pixel, width - 1);
                const size_t sourceIndex =
                    (static_cast<size_t>(row) * width + x) * 4;
                uint16_t pixelU = 0;
                uint16_t pixelV = 0;
                rgbToYuv(bgra[sourceIndex + 2], bgra[sourceIndex + 1],
                         bgra[sourceIndex], fullRange, y[pixel], pixelU,
                         pixelV);
                const size_t pair = pixel / 2;
                if ((pixel % 2) == 0) {
                    u[pair] = pixelU;
                    v[pair] = pixelV;
                } else {
                    u[pair] = static_cast<uint16_t>((u[pair] + pixelU) / 2);
                    v[pair] = static_cast<uint16_t>((v[pair] + pixelV) / 2);
                }
            }

            destination[word++] = u[0] | (static_cast<uint32_t>(y[0]) << 10) |
                                  (static_cast<uint32_t>(v[0]) << 20);
            destination[word++] = y[1] | (static_cast<uint32_t>(u[1]) << 10) |
                                  (static_cast<uint32_t>(y[2]) << 20);
            destination[word++] = v[1] | (static_cast<uint32_t>(y[3]) << 10) |
                                  (static_cast<uint32_t>(u[2]) << 20);
            destination[word++] = y[4] | (static_cast<uint32_t>(v[2]) << 10) |
                                  (static_cast<uint32_t>(y[5]) << 20);
        }
    }
}

bool scaleBgra(PreparedFrame& output, const std::vector<uint8_t>& bgra,
               video_format format, uint32_t width, uint32_t height,
               bool fullRange)
{
    const video_scale_info sourceInfo{VIDEO_FORMAT_BGRA, width, height,
                                      VIDEO_RANGE_FULL, VIDEO_CS_709};
    const video_scale_info destinationInfo{
        format, width, height,
        fullRange ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL, VIDEO_CS_709};
    video_scaler_t* scaler = nullptr;
    if (video_scaler_create(&scaler, &destinationInfo, &sourceInfo,
                            VIDEO_SCALE_POINT) != VIDEO_SCALER_SUCCESS) {
        return false;
    }

    const uint8_t* sourcePlanes[MAX_AV_PLANES] = {bgra.data()};
    const uint32_t sourceLineSizes[MAX_AV_PLANES] = {width * 4};
    const bool result = video_scaler_scale(
        scaler, output.frame.data, output.frame.linesize, sourcePlanes,
        sourceLineSizes);
    video_scaler_destroy(scaler);
    return result;
}

bool prepareFrame(const SourceConfiguration& configuration,
                  PreparedFrame& output, std::string& error)
{
    output = {};
    if (!supportsInputGeometry(configuration.format, configuration.width,
                               configuration.height)) {
        error = "UNSUPPORTED_INPUT_GEOMETRY";
        return false;
    }
    if (!allocateFrameStorage(output, configuration.format,
                              configuration.width, configuration.height,
                              configuration.padding)) {
        error = "invalid frame layout";
        return false;
    }

    // obs_source_output_video() always treats non-YUV frames as full range.
    // Describe those formats the same way so OBS does not expand limited RGB.
    const bool effectiveFullRange =
        configuration.fullRange || !format_is_yuv(configuration.format);

    output.frame.width = configuration.width;
    output.frame.height = configuration.height;
    output.frame.format = configuration.format;
    output.frame.full_range = effectiveFullRange;
    output.frame.flip = configuration.flip;
    output.frame.trc = VIDEO_TRC_SRGB;
    const video_range_type range = effectiveFullRange
                                       ? VIDEO_RANGE_FULL
                                       : VIDEO_RANGE_PARTIAL;
    if (format_is_yuv(configuration.format)) {
        video_format_get_parameters_for_format(
            VIDEO_CS_709, range, configuration.format,
            output.frame.color_matrix, output.frame.color_range_min,
            output.frame.color_range_max);
    }
    return true;
}

bool convertBgra(const SourceConfiguration& configuration,
                 const std::vector<uint8_t>& bgra, PreparedFrame& output)
{
    const bool effectiveFullRange = output.frame.full_range;

    bool converted = false;
    switch (configuration.format) {
    case VIDEO_FORMAT_AYUV:
        fillAyuv(output, bgra, configuration.width, configuration.height,
                 effectiveFullRange);
        converted = true;
        break;
    case VIDEO_FORMAT_V210:
        fillV210(output, bgra, configuration.width, configuration.height,
                 effectiveFullRange);
        converted = true;
        break;
    case VIDEO_FORMAT_R10L:
        fillR10l(output, bgra, configuration.width, configuration.height,
                 effectiveFullRange);
        converted = true;
        break;
    case VIDEO_FORMAT_P216:
        fillP16(output, bgra, configuration.width, configuration.height,
                effectiveFullRange, true);
        converted = true;
        break;
    case VIDEO_FORMAT_P416:
        fillP16(output, bgra, configuration.width, configuration.height,
                effectiveFullRange, false);
        converted = true;
        break;
    default:
        converted = scaleBgra(output, bgra, configuration.format,
                              configuration.width, configuration.height,
                              effectiveFullRange);
        break;
    }
    return converted;
}

bool parseBooleanToken(const std::string& token, bool& value)
{
    if (token == "1" || equalAsciiCaseInsensitive(token, "true")) {
        value = true;
        return true;
    }
    if (token == "0" || equalAsciiCaseInsensitive(token, "false")) {
        value = false;
        return true;
    }
    return false;
}

bool parseUnsignedToken(const std::string& token, uint64_t& value)
{
    std::istringstream input(token);
    input >> value;
    return input && input.peek() == std::char_traits<char>::eof();
}

bool parseControl(const std::string& text, const std::string& framePath,
                  const std::string& blankFramePath,
                  SourceConfiguration& configuration)
{
    uint64_t sequence = 0;
    std::string formatToken;
    uint64_t padding = 0;
    std::string fullRangeToken;
    std::string flipToken = "0";
    uint64_t width = kDefaultWidth;
    uint64_t height = kDefaultHeight;
    uint64_t fps = kDefaultFps;
    std::istringstream input(text);
    if (!(input >> sequence >> formatToken >> padding >> fullRangeToken)) {
        return false;
    }

    std::vector<std::string> optionalTokens;
    std::string token;
    while (input >> token) {
        optionalTokens.push_back(std::move(token));
    }
    if (optionalTokens.size() != 0 && optionalTokens.size() != 1 &&
        optionalTokens.size() != 4 && optionalTokens.size() != 5) {
        return false;
    }
    if (!optionalTokens.empty()) {
        flipToken = optionalTokens[0];
    }
    if (optionalTokens.size() >= 4 &&
        (!parseUnsignedToken(optionalTokens[1], width) ||
         !parseUnsignedToken(optionalTokens[2], height) ||
         !parseUnsignedToken(optionalTokens[3], fps))) {
        return false;
    }

    const FormatEntry* format = findFormat(formatToken);
    bool fullRange = false;
    bool flip = false;
    if (!format || padding > 4096 || width < kMinimumDimension ||
        width > kMaximumWidth || height < kMinimumDimension ||
        height > kMaximumHeight || fps == 0 || fps > kMaximumFps ||
        !parseBooleanToken(fullRangeToken, fullRange) ||
        !parseBooleanToken(flipToken, flip)) {
        return false;
    }

    configuration.sequence = sequence;
    configuration.format = format->format;
    configuration.padding = static_cast<uint32_t>(padding);
    configuration.fullRange = fullRange;
    configuration.flip = flip;
    configuration.width = static_cast<uint32_t>(width);
    configuration.height = static_cast<uint32_t>(height);
    configuration.fps = static_cast<uint32_t>(fps);
    configuration.framePath = framePath;
    if (optionalTokens.size() == 5) {
        if (equalAsciiCaseInsensitive(optionalTokens[4], "blank")) {
            configuration.framePath = blankFramePath;
        } else if (!equalAsciiCaseInsensitive(optionalTokens[4], "frame")) {
            return false;
        }
    }
    if (configuration.framePath.empty()) {
        return false;
    }
    return true;
}

bool readTextFile(const std::string& path, std::string& text)
{
    if (path.empty()) {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) {
        return false;
    }
    text = contents.str();
    return true;
}

void writeStatus(const SourceConfiguration& configuration,
                 const std::string& state, const std::string& detail)
{
    const std::string path = environmentValue(kStatusPathEnvironment);
    if (path.empty()) {
        return;
    }

    std::ostringstream contents;
    contents << configuration.sequence << ' '
             << formatName(configuration.format) << ' '
             << configuration.padding << ' '
             << (configuration.fullRange ? 1 : 0) << ' '
             << (configuration.flip ? 1 : 0) << ' ' << state;
    if (!detail.empty()) {
        contents << ' ' << detail;
    }
    contents << " width=" << configuration.width
             << " height=" << configuration.height
             << " fps=" << configuration.fps << '\n';

    for (int attempt = 0; attempt < 50; ++attempt) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (output) {
            output << contents.str();
            output.flush();
            if (output.good()) {
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    blog(LOG_WARNING,
         "[ciciobsformattest] could not write status file: %s",
         path.c_str());
}

struct FilterControlCommand {
    uint64_t sequence = 0;
    bool apply = false;
    std::string sourceName;
    std::string filterName;
};

bool parseFilterControl(const std::string& text, FilterControlCommand& command)
{
    std::istringstream input(text);
    std::string sequenceToken;
    std::string actionToken;
    if (!std::getline(input, sequenceToken, '\t') ||
        !std::getline(input, actionToken, '\t') ||
        !std::getline(input, command.sourceName, '\t') ||
        !std::getline(input, command.filterName)) {
        return false;
    }
    try {
        command.sequence = std::stoull(sequenceToken);
    } catch (...) {
        return false;
    }
    if (command.sequence == 0 || command.sourceName.empty() ||
        command.filterName.empty()) {
        return false;
    }
    if (equalAsciiCaseInsensitive(actionToken, "APPLY")) {
        command.apply = true;
    } else if (equalAsciiCaseInsensitive(actionToken, "REMOVE")) {
        command.apply = false;
    } else {
        return false;
    }
    return true;
}

void writeFilterControlStatus(uint64_t sequence, const char* state,
                              const std::string& detail)
{
    const std::string path = environmentValue(kFilterStatusPathEnvironment);
    if (path.empty()) {
        return;
    }
    for (int attempt = 0; attempt < 50; ++attempt) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (output) {
            output << sequence << '\t' << state << '\t' << detail << '\n';
            output.flush();
            if (output.good()) {
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    blog(LOG_WARNING,
         "[ciciobsformattest] could not write filter status file: %s",
         path.c_str());
}

void findCiciFilter(obs_source_t*, obs_source_t* filter, void* data)
{
    auto** result = static_cast<obs_source_t**>(data);
    if (*result) {
        return;
    }
    const char* id = obs_source_get_unversioned_id(filter);
    if (id && std::strcmp(id, "cicbeauty") == 0) {
        *result = obs_source_get_ref(filter);
    }
}

void executeFilterControl(void* data)
{
    std::unique_ptr<FilterControlCommand> command(
        static_cast<FilterControlCommand*>(data));
    obs_source_t* source = obs_get_source_by_name(command->sourceName.c_str());
    if (!source) {
        writeFilterControlStatus(command->sequence, "ERROR",
                                 "source was not found");
        return;
    }

    obs_source_t* filter = nullptr;
    obs_source_enum_filters(source, findCiciFilter, &filter);
    bool success = true;
    std::string detail;
    if (command->apply) {
        if (!filter) {
            obs_data_t* settings = obs_data_create();
            filter = obs_source_create("cicbeauty", command->filterName.c_str(),
                                       settings, nullptr);
            obs_data_release(settings);
            if (!filter) {
                success = false;
                detail = "could not create cicbeauty filter";
            } else {
                obs_source_filter_add(source, filter);
                detail = "filter applied on UI task";
            }
        } else {
            detail = "filter already applied";
        }
    } else {
        if (filter) {
            obs_source_filter_remove(source, filter);
            detail = "filter removed on UI task";
        } else {
            detail = "filter already removed";
        }
    }

    if (filter) {
        obs_source_release(filter);
    }
    obs_source_release(source);
    writeFilterControlStatus(command->sequence,
                             success ? "OK" : "ERROR", detail);
    blog(success ? LOG_INFO : LOG_ERROR,
         "[ciciobsformattest] filter control sequence=%llu action=%s "
         "source=%s state=%s detail=%s",
         static_cast<unsigned long long>(command->sequence),
         command->apply ? "APPLY" : "REMOVE",
         command->sourceName.c_str(), success ? "OK" : "ERROR",
         detail.c_str());
}

void pollFilterControl()
{
    const std::string controlPath =
        environmentValue(kFilterControlPathEnvironment);
    std::string previousText;
    while (!gFilterControlStop.load(std::memory_order_acquire)) {
        std::string text;
        if (readTextFile(controlPath, text) && text != previousText) {
            FilterControlCommand parsed;
            if (parseFilterControl(text, parsed)) {
                previousText = text;
                auto command = std::make_unique<FilterControlCommand>(
                    std::move(parsed));
                obs_queue_task(OBS_TASK_UI, executeFilterControl,
                               command.release(), false);
            } else if (!text.empty()) {
                blog(LOG_WARNING,
                     "[ciciobsformattest] invalid filter control command");
            }
        }
        std::this_thread::sleep_for(kControlPollInterval);
    }
}

SourceConfiguration currentSettings(FormatTestSource* source,
                                    uint64_t& revision)
{
    std::lock_guard lock(source->settingsMutex);
    revision = source->settingsRevision;
    return source->settings;
}

void outputFrames(FormatTestSource* source)
{
    const std::string controlPath = environmentValue(kControlPathEnvironment);
    const std::string environmentFramePath =
        environmentValue(kFramePathEnvironment);
    const std::string environmentBlankFramePath =
        environmentValue(kBlankFramePathEnvironment);
    std::string previousControlText;
    uint64_t appliedSettingsRevision = std::numeric_limits<uint64_t>::max();
    SourceConfiguration activeConfiguration;
    PreparedFrame preparedFrame;
    BgraClipReader clip;
    std::vector<uint8_t> bgra =
        makeFallbackBgra(kDefaultWidth, kDefaultHeight);
    bool frameReady = false;
    bool reportReady = false;
    uint64_t framesSinceConfiguration = 0;
    auto nextControlPoll = std::chrono::steady_clock::now();
    uint64_t nextTimestamp = os_gettime_ns();

    while (!source->stop.load(std::memory_order_acquire)) {
        bool configurationChanged = false;
        const auto now = std::chrono::steady_clock::now();
        if (!controlPath.empty() && now >= nextControlPoll) {
            nextControlPoll = now + kControlPollInterval;
            std::string controlText;
            if (readTextFile(controlPath, controlText) &&
                controlText != previousControlText) {
                SourceConfiguration controlConfiguration;
                if (parseControl(controlText, environmentFramePath,
                                 environmentBlankFramePath,
                                 controlConfiguration)) {
                    activeConfiguration = std::move(controlConfiguration);
                    configurationChanged = true;
                    previousControlText = std::move(controlText);
                } else {
                    blog(LOG_WARNING,
                         "[ciciobsformattest] invalid control command: %s",
                         controlText.c_str());
                }
            }
        }

        if (controlPath.empty()) {
            uint64_t settingsRevision = 0;
            SourceConfiguration settings =
                currentSettings(source, settingsRevision);
            if (settingsRevision != appliedSettingsRevision) {
                activeConfiguration = std::move(settings);
                appliedSettingsRevision = settingsRevision;
                configurationChanged = true;
            }
        }

        if (configurationChanged) {
            nextTimestamp = os_gettime_ns();
            framesSinceConfiguration = 0;
            std::string error;
            frameReady = prepareFrame(activeConfiguration, preparedFrame, error);
            if (frameReady) {
                if (clip.open(activeConfiguration.framePath,
                              activeConfiguration.width,
                              activeConfiguration.height)) {
                    preparedFrame.inputKind = clip.frameCount > 1
                                                  ? "EXTERNAL_VIDEO"
                                                  : "EXTERNAL";
                } else {
                    preparedFrame.inputKind = "FALLBACK";
                    bgra = makeFallbackBgra(activeConfiguration.width,
                                            activeConfiguration.height);
                }
                reportReady = true;
            } else {
                blog(LOG_ERROR,
                     "[ciciobsformattest] ERROR sequence=%llu format=%s: %s",
                     static_cast<unsigned long long>(
                         activeConfiguration.sequence),
                     formatName(activeConfiguration.format), error.c_str());
                writeStatus(activeConfiguration, "ERROR", error);
                obs_source_output_video(source->source, nullptr);
            }
        }

        if (frameReady) {
            if (clip.frameCount > 0 && !clip.readNext(bgra)) {
                blog(LOG_ERROR,
                     "[ciciobsformattest] failed to read BGRA video frame");
                writeStatus(activeConfiguration, "ERROR",
                            "video frame read failed");
                frameReady = false;
                obs_source_output_video(source->source, nullptr);
                continue;
            }
            if (!convertBgra(activeConfiguration, bgra, preparedFrame)) {
                blog(LOG_ERROR,
                     "[ciciobsformattest] format conversion failed: %s",
                     formatName(activeConfiguration.format));
                writeStatus(activeConfiguration, "ERROR",
                            "format conversion failed");
                frameReady = false;
                obs_source_output_video(source->source, nullptr);
                continue;
            }
            if (reportReady) {
                blog(LOG_INFO,
                     "[ciciobsformattest] READY sequence=%llu format=%s "
                     "width=%u height=%u fps=%u padding=%u full_range=%d "
                     "flip=%d input=%s frames=%llu",
                     static_cast<unsigned long long>(
                         activeConfiguration.sequence),
                     formatName(activeConfiguration.format),
                     activeConfiguration.width,
                     activeConfiguration.height,
                     activeConfiguration.fps,
                     activeConfiguration.padding,
                     activeConfiguration.fullRange ? 1 : 0,
                     activeConfiguration.flip ? 1 : 0,
                     preparedFrame.inputKind.c_str(),
                     static_cast<unsigned long long>(clip.frameCount));
                writeStatus(activeConfiguration, "READY",
                            preparedFrame.inputKind);
                reportReady = false;
            }
            preparedFrame.frame.timestamp = nextTimestamp;
            obs_source_output_video(source->source, &preparedFrame.frame);
            ++framesSinceConfiguration;
            if (activeConfiguration.sequence > 0 &&
                framesSinceConfiguration ==
                    static_cast<uint64_t>(activeConfiguration.fps) * 2 &&
                !requestScreenshot(source->source, activeConfiguration)) {
                blog(LOG_WARNING,
                     "[ciciobsformattest] screenshot request skipped "
                     "sequence=%llu format=%s",
                     static_cast<unsigned long long>(
                         activeConfiguration.sequence),
                     formatName(activeConfiguration.format));
            }
        }

        const uint64_t frameDuration =
            1'000'000'000ULL / std::max(1U, activeConfiguration.fps);
        nextTimestamp += frameDuration;
        os_sleepto_ns(nextTimestamp);
    }
}

const char* sourceName(void*)
{
    return "CiCi Format Test Source";
}

void updateSource(void* data, obs_data_t* settings)
{
    auto* source = static_cast<FormatTestSource*>(data);
    SourceConfiguration configuration;
    const char* formatValue = obs_data_get_string(settings, kFormatSetting);
    const FormatEntry* format = findFormat(formatValue ? formatValue : "");
    configuration.format = format ? format->format : VIDEO_FORMAT_NV12;
    configuration.padding = static_cast<uint32_t>(std::clamp<int64_t>(
        obs_data_get_int(settings, kPaddingSetting), 0, 4096));
    configuration.fullRange =
        obs_data_get_bool(settings, kFullRangeSetting);
    configuration.flip = obs_data_get_bool(settings, kFlipSetting);
    configuration.width = static_cast<uint32_t>(std::clamp<int64_t>(
        obs_data_get_int(settings, kWidthSetting), kMinimumDimension,
        kMaximumWidth));
    configuration.height = static_cast<uint32_t>(std::clamp<int64_t>(
        obs_data_get_int(settings, kHeightSetting), kMinimumDimension,
        kMaximumHeight));
    configuration.fps = static_cast<uint32_t>(std::clamp<int64_t>(
        obs_data_get_int(settings, kFpsSetting), 1, kMaximumFps));
    const char* framePath = obs_data_get_string(settings, kFramePathSetting);
    configuration.framePath = framePath ? framePath : "";

    std::lock_guard lock(source->settingsMutex);
    configuration.sequence = source->settingsRevision + 1;
    source->settings = std::move(configuration);
    ++source->settingsRevision;
}

void* createSource(obs_data_t* settings, obs_source_t* obsSource)
{
    auto* source = new FormatTestSource;
    source->source = obsSource;
    updateSource(source, settings);
    try {
        source->outputThread = std::thread(outputFrames, source);
    } catch (...) {
        delete source;
        return nullptr;
    }
    return source;
}

void destroySource(void* data)
{
    auto* source = static_cast<FormatTestSource*>(data);
    if (!source) {
        return;
    }
    source->stop.store(true, std::memory_order_release);
    if (source->outputThread.joinable()) {
        source->outputThread.join();
    }
    delete source;
}

void sourceDefaults(obs_data_t* settings)
{
    obs_data_set_default_string(settings, kFormatSetting, "NV12");
    obs_data_set_default_int(settings, kPaddingSetting, 0);
    obs_data_set_default_bool(settings, kFullRangeSetting, false);
    obs_data_set_default_bool(settings, kFlipSetting, false);
    obs_data_set_default_int(settings, kWidthSetting, kDefaultWidth);
    obs_data_set_default_int(settings, kHeightSetting, kDefaultHeight);
    obs_data_set_default_int(settings, kFpsSetting, kDefaultFps);
    obs_data_set_default_string(settings, kFramePathSetting, "");
}

obs_properties_t* sourceProperties(void*)
{
    obs_properties_t* properties = obs_properties_create();
    obs_property_t* formatProperty = obs_properties_add_list(
        properties, kFormatSetting, "Video format", OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_STRING);
    for (const auto& format : kFormats) {
        obs_property_list_add_string(formatProperty, format.name, format.name);
    }
    obs_properties_add_int(properties, kPaddingSetting,
                           "Extra stride bytes per plane", 0, 4096, 1);
    obs_properties_add_bool(properties, kFullRangeSetting, "Full range");
    obs_properties_add_bool(properties, kFlipSetting, "Set frame flip flag");
    obs_properties_add_int(properties, kWidthSetting, "Width",
                           kMinimumDimension, kMaximumWidth, 1);
    obs_properties_add_int(properties, kHeightSetting, "Height",
                           kMinimumDimension, kMaximumHeight, 1);
    obs_properties_add_int(properties, kFpsSetting, "Frames per second", 1,
                           kMaximumFps, 1);
    obs_properties_add_path(properties, kFramePathSetting,
                            "Raw BGRA frame sequence", OBS_PATH_FILE,
                            "Raw BGRA frame (*.bgra);;All files (*.*)", nullptr);
    return properties;
}

obs_source_info kSourceInfo = {
    .id = "cici_obs_format_test_source",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE,
    .get_name = sourceName,
    .create = createSource,
    .destroy = destroySource,
    .get_defaults = sourceDefaults,
    .get_properties = sourceProperties,
    .update = updateSource,
};

} // namespace

extern "C" bool obs_module_load(void)
{
    obs_frontend_add_event_callback(onFrontendEvent, nullptr);
    obs_register_source(&kSourceInfo);
    blog(LOG_INFO,
         "[ciciobsformattest] loaded; control=%s status=%s frame=%s blank=%s "
         "filter_control=%s filter_status=%s",
         environmentValue(kControlPathEnvironment).c_str(),
         environmentValue(kStatusPathEnvironment).c_str(),
         environmentValue(kFramePathEnvironment).c_str(),
         environmentValue(kBlankFramePathEnvironment).c_str(),
         environmentValue(kFilterControlPathEnvironment).c_str(),
         environmentValue(kFilterStatusPathEnvironment).c_str());
    if (!environmentValue(kFilterControlPathEnvironment).empty() &&
        !environmentValue(kFilterStatusPathEnvironment).empty()) {
        gFilterControlStop.store(false, std::memory_order_release);
        gFilterControlThread = std::thread(pollFilterControl);
    }
    return true;
}

extern "C" void obs_module_unload(void)
{
    gFilterControlStop.store(true, std::memory_order_release);
    if (gFilterControlThread.joinable()) {
        gFilterControlThread.join();
    }
    obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
}
