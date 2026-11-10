// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <stdint.h>
#include <string>

#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include "stb/stb_image_resize2.h"

class StbImage {
public:
	StbImage(const char8_t *file);
    StbImage(const std::u8string& file): StbImage(file.c_str()) {}
    ~StbImage();
    
public:
    bool load();
	void store(const char8_t *file);
    void store(const std::u8string& file) { store(file.c_str()); }
    StbImage resizeTo(uint32_t newW, uint32_t newH);
    void resizeSelf(uint32_t newW, uint32_t newH);
    
    bool isValid() const {return img_ != nullptr;};
    uint32_t width() const;
    uint32_t height() const;
    uint32_t channel() const;
    uint8_t* image() const;
    
public:
    static void resize(const uint8_t* src, int32_t srcW, int32_t srcH, uint8_t* dst, int32_t dstW, int32_t dstH);
    
private:
    std::u8string file_;
    uint8_t* img_ = nullptr;    //allocated by stb_image_loadxxx, must be destroyed by stbi_image_free!!
    int32_t width_ = 0;
    int32_t height_ = 0;
    uint8_t* img2_ = nullptr;   //allocated by malloc when been resized, must be destroyed by free!!
    int32_t width2_ = 0;
    int32_t height2_ = 0;
    int32_t channel_ = 0;
};
