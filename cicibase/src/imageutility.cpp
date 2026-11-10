// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include <assert.h>

#include "log.h"
#include "imageutility.h"


StbImage::StbImage(const char8_t *file) : file_(file) {
    channel_ = 4;   //rgba
}

StbImage::~StbImage() {
    if (img_ != nullptr) {
        stbi_image_free(img_);
        img_ = nullptr;
    }
    
    if (img2_ != nullptr) {
        free(img2_);
        img2_ = nullptr;
    }
}

bool StbImage::load() {
    int c = 0;
    img_ = stbi_load(to_char(file_), &width_, &height_, &c, channel_);
    if (img_ == nullptr) {
        LogError<<"stbi_load failed"<<logEnd;
    }
    return img_ != nullptr;
}

void StbImage::store(const char8_t *file) {
    assert(img_ != nullptr);
    auto r = stbi_write_png(to_char(file), width_, height_, channel_, img_, width_*channel_);
    assert(r == 1);
    if (r != 1) {
        LogError<<"stbi_write_png failed"<<logEnd;
    }
}

StbImage StbImage::resizeTo(uint32_t newW, uint32_t newH) {
    assert(false);
    return StbImage(file_.c_str());
}

void StbImage::resizeSelf(uint32_t newW, uint32_t newH) {
    if (newW == width_ && newH == height_) {    //same as original image
        return;
    }
    
    if (newW == width2_ && newH == height2_) {  //same as image resized previously
        return ;
    }
    
    if (img2_ != nullptr) {
        assert(width2_ != 0);
        assert(height2_ != 0);
        free(img2_);
        img2_ = nullptr;
    }
    
    img2_ = stbir_resize_uint8_srgb(img_, width_, height_, 0, 0, newW, newH, 0, STBIR_RGBA);
    if (img2_ != nullptr) {
        width2_ = newW;
        height2_ = newH;
        
        //auto r = stbi_write_png("E:\\study\\xx2.png", width2_, height2_, 4, img2_, width2_*4);
        //assert(r == 1);
    }
}

uint32_t StbImage::width() const {
    if (img2_ != nullptr) {
        assert(width2_ != 0);
        assert(height2_ != 0);
        return width2_;
    }
    
    return width_;
}

uint32_t StbImage::height() const {
    if (img2_ != nullptr) {
        assert(width2_ != 0);
        assert(height2_ != 0);
        return height2_;
    }
    
    return height_;
}

uint32_t StbImage::channel() const {
    assert(channel_ == 4);
    return channel_;
}

uint8_t* StbImage::image() const {
    if (img2_ != nullptr) {
        return img2_;
    }
    
    return img_;
}

void StbImage::resize(const uint8_t* src, int32_t srcW, int32_t srcH, uint8_t* dst, int32_t dstW, int32_t dstH) {
    stbir_resize_uint8_srgb(src, srcW, srcH, 0, dst, dstW, dstH, 0, STBIR_RGBA);
}
