// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include "imageutility.h"

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() /
                ("cici-image-path-tests-" +
                 std::to_string(std::chrono::steady_clock::now()
                                    .time_since_epoch()
                                    .count()))) {
        std::error_code error;
        std::filesystem::create_directories(path_, error);
        if (error) {
            path_.clear();
        }
    }

    ~TemporaryDirectory() {
        if (!path_.empty()) {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace

int main() {
    TemporaryDirectory temporary;
    if (temporary.path().empty()) {
        std::cerr << "failed to create temporary directory\n";
        return 1;
    }

    const std::filesystem::path imageDirectory =
        temporary.path() / std::filesystem::path(u8"\u4e2d\u6587\u80cc\u666f\u56fe\u7247");
    const std::filesystem::path input = imageDirectory / u8"\u6d4b\u8bd5\u8f93\u5165.png";
    const std::filesystem::path output = imageDirectory / u8"\u6d4b\u8bd5\u8f93\u51fa.png";

    std::error_code error;
    std::filesystem::create_directories(imageDirectory, error);
    if (error || !std::filesystem::copy_file(
                     std::filesystem::path(CICI_TEST_IMAGE_PATH), input,
                     std::filesystem::copy_options::overwrite_existing, error)) {
        std::cerr << "failed to prepare image fixture\n";
        return 1;
    }

    StbImage image(input.u8string());
    if (!image.load() || image.width() == 0 || image.height() == 0) {
        std::cerr << "failed to load image from UTF-8 path\n";
        return 1;
    }

    image.store(output.u8string());
    if (!std::filesystem::is_regular_file(output)) {
        std::cerr << "failed to store image at UTF-8 path\n";
        return 1;
    }

    return 0;
}
