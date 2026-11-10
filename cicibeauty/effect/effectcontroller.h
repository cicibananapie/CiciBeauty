// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once
#include <cstdint>
#include <functional>
#include "../obsinfo.h"

// Local preview facade. Effect/configuration execution belongs to ciciluma.
// Native preview transport, color picking and frame transfer are the next step.
class EffectController {
public:
    enum NativeRenderSourceType {
        None,
        Input,
        InputWithMagnifier,
        Output
    };
    using NativeRenderTargetId = uint64_t;
    using NativeSizeCallback = std::function<void(int, int)>;
    using ColorPickCallback = std::function<void(uint32_t)>;
    using AutoKeyingCallback = std::function<void(uint32_t, float)>;
    explicit EffectController(ObsInfo* info) : obsInfo_(info) {
    }
    void getRenderSize(int& w, int& h);
    NativeRenderTargetId registerNativeRenderTarget(void*, int, int, NativeRenderSourceType, NativeSizeCallback);
    void unregisterNativeRenderTarget(NativeRenderTargetId);
    void updateColorPickMagnifier(int, int, float, float, bool);
    void requestColorPick(ColorPickCallback);
    void autoKeying(AutoKeyingCallback);

private:
    ObsInfo* obsInfo_;
};
