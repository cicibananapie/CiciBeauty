// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "effectcontroller.h"
#include "log.h"

namespace {
void pendingPreview() {
    LogWarn << "Native preview/frame transport to ciciluma is not connected yet" << logEnd;
    assert(false && "TODO: connect ciciluma preview/frame transport");
}
}
void EffectController::getRenderSize(int& w, int& h) {
    w = static_cast<int>(obsInfo_->frameWidth());
    h = static_cast<int>(obsInfo_->frameHeight());
    const auto rotation = obsInfo_->canvasTransform().rotation_;
    if (rotation == ObsInfo::Rotate_90 || rotation == ObsInfo::Rotate_270) {
        std::swap(w, h);
    }
}
EffectController::NativeRenderTargetId
EffectController::registerNativeRenderTarget(void*, int, int, NativeRenderSourceType, NativeSizeCallback) {
    pendingPreview();
    return 0;
}
void EffectController::unregisterNativeRenderTarget(NativeRenderTargetId id) {
    assert(id == 0);
}
void EffectController::updateColorPickMagnifier(int, int, float, float, bool) {
    pendingPreview();
}
void EffectController::requestColorPick(ColorPickCallback) {
    pendingPreview();
}
void EffectController::autoKeying(AutoKeyingCallback) {
    pendingPreview();
}
